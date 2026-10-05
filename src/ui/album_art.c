/* Shared asynchronous artwork service for posters, episode thumbs and album art.
 * Separate artwork and metadata workers keep slow posters from blocking shelves. Only the frame thread touches GPU objects. */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui/album_art.h"
#include "util/cache.h"
#include "util/net.h"
#include "util/log.h"
#include "util/stb_image.h"

#define SLOT_COUNT 16
#define TEX_W 128
#define TEX_H 256
#define MAX_JPEG (512 * 1024)
enum { EMPTY, QUEUED, WORKING, PIXELS_READY, DISPLAY_READY, FAILED };
typedef struct {
    char key[24];
    jfin_item_t item;
    jfin_session_t session;
    unsigned generation;
    unsigned last_frame, retry_frame;
    int state, width, height;
    u16 *pixels;
    bool tex_initialized, cache_only;
    C3D_Tex tex;
    Tex3DS_SubTexture subtex;
} art_slot_t;
static art_slot_t s_slots[SLOT_COUNT];
static LightLock s_lock;
static Thread s_worker, s_browser_worker;
static bool s_stop;
static unsigned s_frame;
static char s_play_key[24];
static struct {
    unsigned generation;
    bool queued, ready, success;
    char id[JFIN_MAX_ID];
    jfin_session_t session;
    jfin_item_details_t result;
} s_detail;
static struct {
    unsigned generation;
    int next_row;
    jfin_session_t session;
    bool ready[5], success[5];
    jfin_item_list_t rows[5];
} s_home;

typedef struct { u8 *data; size_t size; } download_t;
static size_t write_image(void *ptr, size_t size, size_t count, void *context)
{
    download_t *d = context;
    if (size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    if (d->size > MAX_JPEG || n > MAX_JPEG - d->size) return 0;
    if (__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) return 0;
    u8 *buf = realloc(d->data, d->size + n);
    if (!buf && n) return 0;
    d->data = buf;
    memcpy(d->data + d->size, ptr, n);
    d->size += n;
    return n;
}
static int image_progress(void *p, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)p; (void)a; (void)b; (void)c; (void)d;
    return __atomic_load_n(&s_stop, __ATOMIC_ACQUIRE) ? 1 : 0;
}
static const char *image_id(const jfin_item_t *item)
{
    if (item->has_primary_image) return item->id;
    if (item->has_album_image) return item->album_id;
    if (item->has_series_image) return item->series_id;
    return NULL;
}
static bool item_key(const jfin_session_t *session, const jfin_item_t *item, char key[24])
{
    const char *id = image_id(item);
    if (!id || !*id) return false;
    cache_art_key(session->server_url, session->user_id, id, key, 24);
    return key[0] != '\0';
}
static u16 *fetch_pixels(const jfin_session_t *session, const jfin_item_t *item,
                         const char *key, int *width, int *height, bool cache_only)
{
    download_t d = {0};
    bool cached = cache_art_read(key, &d.data, &d.size);
    if(!cached && cache_only)return NULL;
    if (!cached) {
        char url[JFIN_URL_BUF];
        jfin_get_image_url_for_item(session, item, TEX_W, 192, url, sizeof(url));
        if (!url[0]) return NULL;
        CURL *curl = curl_easy_init();
        if (!curl) return NULL;
        char token[JFIN_MAX_TOKEN + 32];
        int n = snprintf(token, sizeof(token), "X-Emby-Token: %s", session->access_token);
        if (n < 0 || n >= (int)sizeof(token)) { curl_easy_cleanup(curl); return NULL; }
        struct curl_slist *headers = curl_slist_append(NULL, token);
        if (!headers) { curl_easy_cleanup(curl); return NULL; }
        net_configure(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_image);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &d);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, image_progress);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        CURLcode result = net_perform_cancelable(curl,&s_stop);
        long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (result != CURLE_OK) {
            log_write("ART: download failed curl=%d HTTP=%ld (%s)", (int)result, status, curl_easy_strerror(result));
            free(d.data); return NULL;
        }
    }
    int w, h, channels;
    /* Check dimensions BEFORE decompressing untrusted bytes. */
    if (!d.size || !stbi_info_from_memory(d.data, (int)d.size, &w, &h, &channels) ||
        w <= 0 || h <= 0 || w > TEX_W || h > 192) { log_write("ART: invalid image or dimensions (%u bytes)", (unsigned)d.size); free(d.data); return NULL; }
    u8 *rgb = stbi_load_from_memory(d.data, (int)d.size, &w, &h, &channels, 3);
    if (!rgb) { log_write("ART: decode failed"); free(d.data); return NULL; }
    u16 *pixels = malloc((size_t)w * h * sizeof(u16));
    if (pixels) {
        for (int i = 0; i < w * h; i++)
            pixels[i] = ((rgb[i * 3] >> 3) << 11) |
                        ((rgb[i * 3 + 1] >> 2) << 5) | (rgb[i * 3 + 2] >> 3);
        if (!cached) cache_art_write(key, d.data, d.size);
        *width = w; *height = h;
        log_write("ART: decoded %dx%d cached=%d", w, h, cached);
    }
    stbi_image_free(rgb); free(d.data);
    return pixels;
}
static void worker(void *arg)
{
    bool metadata = arg != NULL;
    /* Large JSON lists and art jobs live on the heap, not the thread stack. */
    jfin_item_list_t *list = malloc(sizeof(*list));
    art_slot_t *job = malloc(sizeof(*job));
    if (!list || !job) {
        free(list); free(job);
        LightLock_Lock(&s_lock);
        s_home.next_row = 5;
        for (int row = 0; row < 5; row++) { s_home.ready[row] = true; s_home.success[row] = false; }
        LightLock_Unlock(&s_lock); return;
    }
    while (!__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
        jfin_session_t session;
        char id[JFIN_MAX_ID];
        unsigned generation = 0;
        int kind = 0, slot = -1, row = -1;
        LightLock_Lock(&s_lock);
        if (metadata) {
            if (s_detail.queued) {
                kind = 1; s_detail.queued = false;
                session = s_detail.session; generation = s_detail.generation;
                snprintf(id, sizeof(id), "%s", s_detail.id);
            } else if (s_home.next_row < 5) {
                kind = 3; row = s_home.next_row++;
                session = s_home.session; generation = s_home.generation;
            }
        } else {
            for (int i = 0; i < SLOT_COUNT; i++) {
                if (s_slots[i].state == QUEUED) {
                    kind = 2; slot = i; *job = s_slots[i];
                    s_slots[i].state = WORKING; break;
                }
            }
        }
        LightLock_Unlock(&s_lock);
        if (kind == 1) {
            jfin_item_details_t result;
            bool ok = jfin_get_item_details_background(&session, id, &result);
            LightLock_Lock(&s_lock);
            if (generation == s_detail.generation) {
                s_detail.ready = true; s_detail.success = ok;
                if (ok) s_detail.result = result;
            }
            LightLock_Unlock(&s_lock);
        } else if (kind == 2) {
            int w = 0, h = 0;
            u16 *pixels = fetch_pixels(&job->session, &job->item, job->key, &w, &h, job->cache_only);
            LightLock_Lock(&s_lock);
            if (job->generation == s_slots[slot].generation) {
                s_slots[slot].pixels = pixels;
                s_slots[slot].width = w; s_slots[slot].height = h;
                s_slots[slot].state = pixels ? PIXELS_READY : FAILED;
                if (!pixels) s_slots[slot].retry_frame = UINT32_MAX;
            } else free(pixels);
            LightLock_Unlock(&s_lock);
        } else if (kind == 3) {
            memset(list, 0, sizeof(*list));
            bool ok = jfin_get_home_shelf(&session, row, list);
            LightLock_Lock(&s_lock);
            if (generation == s_home.generation) {
                s_home.rows[row] = *list;
                s_home.success[row] = ok; s_home.ready[row] = true;
            }
            LightLock_Unlock(&s_lock);
        } else svcSleepThread(10000000LL);
    }
    free(list); free(job);
}
bool album_art_init(void)
{
    LightLock_Init(&s_lock);
    s_home.next_row = 5;
    __atomic_store_n(&s_stop, false, __ATOMIC_RELEASE);
    s32 priority = 0;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    s_worker = threadCreate(worker, NULL, 128 * 1024, priority + 1, -1, false);
    s_browser_worker = threadCreate(worker, (void *)1, 128 * 1024, priority + 1, -1, false);
    if (!s_worker || !s_browser_worker) {
        __atomic_store_n(&s_stop, true, __ATOMIC_RELEASE);
        if (s_worker) { threadJoin(s_worker, U64_MAX); threadFree(s_worker); s_worker = NULL; }
        if (s_browser_worker) { threadJoin(s_browser_worker, U64_MAX); threadFree(s_browser_worker); s_browser_worker = NULL; }
        log_write("ART: unable to start loading workers"); return false;
    }
    log_write("ART: independent artwork and metadata workers started");
    return true;
}
static void request_art(const jfin_session_t *session, const jfin_item_t *item,bool cache_only)
{
    if (!s_worker) return;
    char key[24];
    if (!item_key(session, item, key)) return;
    LightLock_Lock(&s_lock);
    int target = -1;
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (!strcmp(key, s_slots[i].key)) {
            if(s_slots[i].cache_only && !cache_only && s_slots[i].state==FAILED)s_slots[i].state=QUEUED;
            s_slots[i].cache_only=cache_only;
            if (s_slots[i].state == FAILED && s_slots[i].retry_frame == UINT32_MAX)
                s_slots[i].retry_frame = s_frame;
            if (s_slots[i].state == FAILED && s_frame - s_slots[i].retry_frame >= 300) {
                s_slots[i].session = *session; s_slots[i].retry_frame = s_frame;
                s_slots[i].state = QUEUED;
            }
            s_slots[i].last_frame = s_frame;
            LightLock_Unlock(&s_lock); return;
        }
        if (s_slots[i].state != WORKING &&
            (target < 0 || s_slots[i].last_frame < s_slots[target].last_frame)) target = i;
    }
    if (target >= 0) {
        art_slot_t *s = &s_slots[target];
        free(s->pixels); s->pixels = NULL;
        s->generation++; s->cache_only=cache_only; s->item = *item; s->session = *session;
        snprintf(s->key, sizeof(s->key), "%s", key);
        s->last_frame = s->retry_frame = s_frame; s->state = QUEUED;
    }
    LightLock_Unlock(&s_lock);
}
void album_art_request(const jfin_session_t *s,const jfin_item_t *i){request_art(s,i,false);}
void album_art_request_cached(const jfin_session_t *s,const jfin_item_t *i){request_art(s,i,true);}
void album_art_update(void)
{
    s_frame++;
    LightLock_Lock(&s_lock);
    for (int i = 0; i < SLOT_COUNT; i++) {
        art_slot_t *s = &s_slots[i];
        if (s->state != PIXELS_READY) continue;
        if (!s->tex_initialized) s->tex_initialized = C3D_TexInit(&s->tex, TEX_W, TEX_H, GPU_RGB565);
        if (!s->tex_initialized) { free(s->pixels); s->pixels = NULL; s->state = FAILED; continue; }
        C3D_TexSetFilter(&s->tex, GPU_LINEAR, GPU_LINEAR);
        u16 *dest = s->tex.data;
        memset(dest, 0, TEX_W * TEX_H * sizeof(u16));
        for (int y = 0; y < s->height; y++) {
            for (int x = 0; x < s->width; x++) {
                unsigned morton = 0;
                for (int bit = 0; bit < 3; bit++)
                    morton |= ((x >> bit) & 1) << (2 * bit) |
                              ((y >> bit) & 1) << (2 * bit + 1);
                unsigned tile = ((y / 8) * (TEX_W / 8) + (x / 8)) * 64;
                dest[tile + morton] = s->pixels[y * s->width + x];
            }
        }
        C3D_TexFlush(&s->tex);
        s->subtex = (Tex3DS_SubTexture){.width = s->width, .height = s->height,
            .left = 0, .top = 1, .right = (float)s->width / TEX_W,
            .bottom = 1 - (float)s->height / TEX_H};
        free(s->pixels); s->pixels = NULL; s->state = DISPLAY_READY;
    }
    LightLock_Unlock(&s_lock);
}
static int find_ready(const char *key)
{
    for (int i = 0; i < SLOT_COUNT; i++)
        if (s_slots[i].state == DISPLAY_READY && !strcmp(s_slots[i].key, key)) return i;
    return -1;
}
static bool draw_key(const char *key, float x, float y, float width, float height)
{
    /* DISPLAY_READY slots and GPU fields only change on this main thread. */
    LightLock_Lock(&s_lock);
    int index = find_ready(key);
    if (index >= 0) {
        art_slot_t *s = &s_slots[index];
        float scale = width / s->width;
        if (height / s->height < scale) scale = height / s->height;
        C2D_Image image = {.tex = &s->tex, .subtex = &s->subtex};
        C2D_DrawImageAt(image, x + (width - s->width * scale) / 2,
                        y + (height - s->height * scale) / 2, 0.5f, NULL, scale, scale);
    }
    LightLock_Unlock(&s_lock);
    return index >= 0;
}
bool album_art_draw_item(const jfin_session_t *session, const jfin_item_t *item,
                         float x, float y, float w, float h)
{
    char key[24];
    return item_key(session, item, key) && draw_key(key, x, y, w, h);
}
void album_art_request_details(const jfin_session_t *session, const char *id)
{
    LightLock_Lock(&s_lock);
    if (strcmp(id, s_detail.id) || strcmp(session->server_url, s_detail.session.server_url) ||
        strcmp(session->access_token, s_detail.session.access_token)) {
        s_detail.generation++; s_detail.session = *session;
        snprintf(s_detail.id, sizeof(s_detail.id), "%s", id);
        s_detail.queued = true; s_detail.ready = false;
    }
    LightLock_Unlock(&s_lock);
}
bool album_art_take_details(const char *id, jfin_item_details_t *out, bool *success)
{
    LightLock_Lock(&s_lock);
    bool ready = s_detail.ready && !strcmp(id, s_detail.id);
    if (ready) { if (s_detail.success) *out = s_detail.result;
        *success = s_detail.success; s_detail.ready = false; s_detail.id[0] = '\0'; }
    LightLock_Unlock(&s_lock);
    return ready;
}
void album_art_request_home(const jfin_session_t *session)
{
    LightLock_Lock(&s_lock);
    /* Refresh also lets failed image requests retry after a network change. */
    for (int i = 0; i < SLOT_COUNT; i++)
        if (s_slots[i].state == FAILED) { s_slots[i].state = EMPTY; s_slots[i].key[0] = '\0'; }
    s_home.generation++; s_home.session = *session;
    s_home.next_row = 0;
    memset(s_home.ready, 0, sizeof(s_home.ready));
    LightLock_Unlock(&s_lock);
}
bool album_art_take_home(int row, jfin_item_list_t *out, bool *success)
{
    if (row < 0 || row >= 5) return false;
    LightLock_Lock(&s_lock);
    bool ready = s_home.ready[row];
    if (ready) {
        *out = s_home.rows[row]; *success = s_home.success[row];
        s_home.ready[row] = false;
    }
    LightLock_Unlock(&s_lock);
    return ready;
}
bool album_art_load(const jfin_session_t *session, const jfin_item_t *item)
{
    if (!item_key(session, item, s_play_key)) { s_play_key[0] = '\0'; return false; }
    album_art_request(session, item);
    return true;
}
bool album_art_load_cached(const jfin_session_t *session,const jfin_item_t *item) {
    if(!item_key(session,item,s_play_key)){s_play_key[0]=0;return false;}
    album_art_request_cached(session,item);return true;
}
void album_art_draw(float x, float y, float size) { draw_key(s_play_key, x, y, size, size); }
bool album_art_is_loaded(void)
{
    LightLock_Lock(&s_lock);
    bool ready = find_ready(s_play_key) >= 0;
    LightLock_Unlock(&s_lock); return ready;
}
void album_art_request_stop(void) { __atomic_store_n(&s_stop,true,__ATOMIC_RELEASE); }
void album_art_cleanup(void)
{
    album_art_request_stop();
    if (s_worker) { threadJoin(s_worker, U64_MAX); threadFree(s_worker); s_worker = NULL; }
    if (s_browser_worker) { threadJoin(s_browser_worker, U64_MAX); threadFree(s_browser_worker); s_browser_worker = NULL; }
    for (int i = 0; i < SLOT_COUNT; i++) {
        free(s_slots[i].pixels);
        if (s_slots[i].tex_initialized) C3D_TexDelete(&s_slots[i].tex);
    }
    memset(s_slots, 0, sizeof(s_slots));
    memset(&s_detail, 0, sizeof(s_detail));
    memset(&s_home, 0, sizeof(s_home));
    s_play_key[0] = '\0';
}
