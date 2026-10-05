/**
 * ui.c - Dual-screen UI implementation
 *
 * Uses citro2d for GPU-accelerated 2D rendering.
 * Top screen: now-playing / branding
 * Bottom screen: touch-driven list navigation
 *
 * MVP: text-based rendering. Album art loading deferred to phase 2.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <3ds.h>
#include <citro2d.h>

#include "ui/ui.h"
#include "ui/timeline.h"
#include "api/jellyfin.h"
#include "audio/player.h"
#include "video/video_player.h"
#include "ui/album_art.h"
#include "util/cache.h"
#include "util/config.h"
#include "util/log.h"

#include <curl/curl.h>
#include "util/net.h"
#include "util/update.h"

extern jfin_config_t g_config;

/* ── Render targets ────────────────────────────────────────────────── */

static C3D_RenderTarget *s_top       = NULL;
static C3D_RenderTarget *s_top_right = NULL;  /* right eye for stereoscopic 3D */
static C3D_RenderTarget *s_bottom    = NULL;
static C2D_TextBuf       s_text_buf = NULL;
static C2D_TextBuf       s_measure_buf = NULL;
static C2D_Font          s_font = NULL;

/* ── Helpers ───────────────────────────────────────────────────────── */

static u32 rgba(u32 hex)
{
    /* Convert 0xRRGGBBAA to citro2d's ABGR format */
    u8 r = (hex >> 24) & 0xFF;
    u8 g = (hex >> 16) & 0xFF;
    u8 b = (hex >> 8)  & 0xFF;
    u8 a = hex & 0xFF;
    return C2D_Color32(r, g, b, a);
}

static void draw_text_depth(float x, float y, float size, u32 color, const char *text, float depth)
{
    C2D_Text c2d_text;
    C2D_TextParse(&c2d_text, s_text_buf, text);
    C2D_TextOptimize(&c2d_text);
    C2D_DrawText(&c2d_text, C2D_WithColor, x, y, depth, size, size, color);
}

static void draw_text(float x,float y,float size,u32 color,const char *text)
{ draw_text_depth(x,y,size,color,text,0.5f); }

static void draw_rect(float x, float y, float w, float h, u32 color)
{
    C2D_DrawRectSolid(x, y, 0.0f, w, h, color);
}

/* A small rotating ring, animated from the system clock. */
static void draw_loading_ring(float x, float y)
{
    unsigned phase = (unsigned)(osGetTime() / 100) % 8;
    for (unsigned dot = 0; dot < 8; dot++) {
        float angle = dot * 0.78539816f;
        draw_rect(x + cosf(angle) * 12 - 2, y + sinf(angle) * 12 - 2, 4, 4,
                  rgba(dot == phase || dot == (phase + 7) % 8 ? COLOR_PRIMARY : COLOR_SEPARATOR));
    }
}
static const char *s_wait_label = "Loading media...";
static u64 s_wait_last_frame;
static bool s_ui_exiting;
static void draw_wait_frame(void *context)
{
    /* Process HOME/sleep events before touching graphics during a modal wait. */
    if (!aptMainLoop()) {
        audio_player_request_stop();video_player_request_stop();ui_begin_shutdown();return;
    }
    bool force = context != NULL;
    u64 now = osGetTime();
    if (!force && now - s_wait_last_frame < 80) return;
    s_wait_last_frame = now;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(s_text_buf);
    C2D_TargetClear(s_bottom, rgba(COLOR_BG_DARK));
    C2D_SceneBegin(s_bottom);
    draw_loading_ring(160, 95);
    draw_text(50, 125, 0.5f, rgba(COLOR_TEXT_PRIMARY), s_wait_label);
    C3D_FrameEnd(0);
}

static void format_ticks(int64_t ticks, char *out, int out_len)
{
    int total_sec = (int)(ticks / 10000000LL);
    int min = total_sec / 60;
    int sec = total_sec % 60;
    snprintf(out, out_len, "%d:%02d", min, sec);
}

/* Map Jellyfin's 3D metadata onto the video player's render mode.
 * TAB formats aren't renderable in 3D yet — they play flat (2D). */
static vp_3d_mode_t item_3d_mode(const jfin_item_t *item)
{
    switch (item->video_3d_format) {
    case JFIN_3D_HSBS: return VP_3D_HSBS;
    case JFIN_3D_FSBS: return VP_3D_FSBS;
    default:           return VP_3D_NONE;
    }
}

/* ── Playback / offline-cache helpers ──────────────────────────────── */

static bool item_is_video(const jfin_item_t *item)
{
    return item->type == JFIN_ITEM_MOVIE || item->type == JFIN_ITEM_EPISODE;
}

static bool item_is_playable(const jfin_item_t *item)
{
    return item->type == JFIN_ITEM_AUDIO || item_is_video(item);
}

/* Cache size shown in settings — refreshed on entry/clear, never per-frame
 * (cache_total_bytes walks the SD directory) */
static u64 s_cache_bytes_ui;

/**
 * Start playback of a playable item, preferring the offline cache.
 * Covers: video with audio-stream fallback, audio/Old-3DS path, playback
 * reporting, album art, and now-playing state. View transitions stay at
 * the call sites (browse switches to now-playing; auto-advance doesn't).
 */
static bool ui_start_item(ui_state_t *state, const jfin_session_t *session,
                          const jfin_item_t *item, int item_index,
                          int64_t start_ticks)
{
    s_wait_label = "Starting playback...";
    draw_wait_frame((void *)1);
    if (s_ui_exiting) return false;
    bool restarting = state->has_now_playing && !strcmp(state->now_playing.id, item->id);
    state->seek_pause_pending = false;
    jfin_stream_t stream;
    char cpath[512];
    bool started = false;
    download_t saved;
    bool downloaded=download_find(session,item->id,&saved);
    bool offline=state->current_view==VIEW_DOWNLOADS || (restarting && state->playback_offline_only);
    state->playback_from_sd=false;
    if (offline && item->type==JFIN_ITEM_AUDIO && start_ticks!=0) {
        snprintf(state->message,sizeof(state->message),"Offline music seeking is not available yet.");return false;
    }
    if (downloaded && cache_path(saved.key,download_ext(item),cpath,sizeof(cpath))) {
        audio_player_stop();video_player_stop();
        started=item_is_video(item)?video_player_play(cpath,item->runtime_ticks,start_ticks,item_3d_mode(item)):
            audio_player_play(cpath,item->runtime_ticks,0);
        state->playback_from_sd=started;
        /* A broken saved movie must show an error, never switch to streaming. */
        if(!started){state->has_now_playing=false;state->auto_stopped=true;return false;}
    }
    if(offline && !started)return false;

    if (!started && !offline && item_is_video(item) && video_player_is_supported()) {
        vp_3d_mode_t mode_3d = item_3d_mode(item);
        audio_player_stop();
        if (cache_has(item->id, "ts") &&
            cache_path(item->id, "ts", cpath, sizeof(cpath))) {
            /* Local files seek in-place, so start_ticks passes through */
            started = video_player_play(cpath, item->runtime_ticks,
                                        start_ticks, mode_3d);
            state->playback_from_sd=started;
        } else if (jfin_get_video_stream(session, item->id, start_ticks,
                                         mode_3d != VP_3D_NONE, &stream)) {
            started = video_player_play(stream.url, item->runtime_ticks,
                                        start_ticks, mode_3d);
        }
    }

    if (!started && !offline) {
        /* Audio track, Old 3DS, or the video path failed.
         * Cached audio can't seek locally (mpg123 feed API), so a
         * non-zero start position falls back to streaming. */
        video_player_stop();
        if (start_ticks == 0 && cache_has(item->id, "mp3") &&
            cache_path(item->id, "mp3", cpath, sizeof(cpath))) {
            started = audio_player_play(cpath, item->runtime_ticks, 0);
            state->playback_from_sd=started;
        } else if (jfin_get_audio_stream(session, item->id, start_ticks, &stream)) {
            started = audio_player_play(stream.url, item->runtime_ticks,
                                        start_ticks);
        }
    }

    if (started) {
        if (!restarting && item_index >= 0 && item_index < JFIN_MAX_ITEMS &&
            item != &state->play_queue.items[item_index])
            state->play_queue = state->items;
        state->now_playing = *item;
        state->has_now_playing = true;
        state->playing_index = item_index;
        state->auto_stopped = false;
        state->playback_offline_only=offline;
        if(offline){album_art_load_cached(session,item);state->play_queue.count=1;state->play_queue.items[0]=*item;state->playing_index=0;}
        if (!restarting && !offline) {
            jfin_report_start(session, item->id);
            album_art_load(session, item);
        }
    }
    s_wait_label = "Loading media...";
    return started;
}

/* One restart per gesture. Always stop both consumers before reopening. */
static void ui_seek(ui_state_t *state, const jfin_session_t *session,
                    int64_t target, bool paused)
{
    jfin_item_t item = state->now_playing;
    int index = state->playing_index;
    state->seeking = false;
    target = timeline_clamp(target, item.runtime_ticks);
    if(state->playback_offline_only && item.type==JFIN_ITEM_AUDIO) {
        snprintf(state->message,sizeof(state->message),"Offline music seeking is not available yet.");return;
    }
    video_player_stop();
    audio_player_stop();
    if (!ui_start_item(state, session, &item, index, target)) {
        state->has_now_playing = false;
        state->auto_stopped = true;
        snprintf(state->message, sizeof(state->message), "Unable to restart playback after seeking.");
        state->current_view = state->previous_view;
        return;
    }
    state->seek_pause_pending = paused; /* apply after buffering completes */
    if(!state->playback_offline_only)jfin_report_progress(session, item.id, target, paused);
}

/* ── Modal download (blocking, B cancels) ──────────────────────────── */

typedef struct {
    const jfin_item_t *item;
    int64_t            est_total;  /* runtime x bitrate estimate (bytes) */
    u64                last_render_ms;
    bool               cancelled;
} dl_ctx_t;

static void dl_render_progress(dl_ctx_t *dl, s64 dlnow)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(s_text_buf);
    C2D_TargetClear(s_bottom, rgba(COLOR_BG_DARK));
    C2D_SceneBegin(s_bottom);
    draw_text(10, 10, 0.6f, rgba(COLOR_PRIMARY), "Downloading");
    draw_text(10, 40, 0.5f, rgba(COLOR_TEXT_PRIMARY), dl->item->name);

    char line[96];
    double mb = (double)dlnow / (1024.0 * 1024.0);
    if (dl->est_total > 0) {
        int pct = (int)((double)dlnow * 100.0 / (double)dl->est_total);
        if (pct > 99) pct = 99; /* size is an estimate — never claim done */
        draw_rect(20, 80, 280, 8, rgba(COLOR_BG_CARD));
        draw_rect(20, 80, 280.0f * pct / 100.0f, 8, rgba(COLOR_PRIMARY));
        snprintf(line, sizeof(line), "%.1f MB  (~%d%%)", mb, pct);
    } else {
        snprintf(line, sizeof(line), "%.1f MB", mb);
    }
    draw_text(20, 100, 0.5f, rgba(COLOR_VALUE), line);
    draw_text(10, 140, 0.4f, rgba(COLOR_TEXT_SECONDARY),
              "Keep the lid open — closing it drops WiFi.");
    draw_text(10, 210, 0.45f, rgba(COLOR_TEXT_SECONDARY), "B: cancel");
    C3D_FrameEnd(0);
}

static int dl_progress_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow,
                          curl_off_t ultotal, curl_off_t ulnow)
{
    (void)dltotal; (void)ultotal; (void)ulnow;
    dl_ctx_t *dl = (dl_ctx_t *)ud;

    if (!aptMainLoop()) {
        audio_player_request_stop();video_player_request_stop();ui_begin_shutdown();
        dl->cancelled=true;return 1;
    }
    hidScanInput();
    if (hidKeysDown() & KEY_B) {
        dl->cancelled = true;
        return 1; /* abort the transfer */
    }

    u64 now = osGetTime();
    if (now - dl->last_render_ms >= 250) {
        dl->last_render_ms = now;
        dl_render_progress(dl, (s64)dlnow);
    }
    return 0;
}

static bool dl_pump(uint64_t bytes,uint64_t total,void *context) {
    dl_ctx_t *dl=context;
    if(total>0 && total<INT64_MAX)dl->est_total=(int64_t)total;
    return dl_progress_cb(dl,(curl_off_t)total,(curl_off_t)bytes,0,0)==0;
}
static void reload_downloads(ui_state_t *state,const jfin_session_t *session) {
    state->download_count=download_list(session,state->downloads,JFIN_MAX_ITEMS);
    if(state->download_selected>=state->download_count)state->download_selected=0;
    state->download_scroll=0;
}
static void ui_download_item(ui_state_t *state,const jfin_session_t *session,
                             const jfin_item_t *item)
{
    download_t saved;
    if(download_find(session,item->id,&saved)) {
        snprintf(state->message,sizeof(state->message),"Already saved. Open Downloads to play or delete.");return;
    }
    jfin_item_details_t details={0};details.item=*item;
    if(state->current_view==VIEW_DETAILS && !state->details_loading)details=state->details;
    else if(state->preview_ready && !strcmp(state->preview.item.id,item->id))details=state->preview;
    /* If the index is full, a completed download would be renamed on disk
     * but never registered, so cache_has() stays false and the next X-press
     * would delete the good file. Refuse up front with a visible message. */
    if (cache_is_full() || download_list(session,NULL,JFIN_MAX_ITEMS)>=JFIN_MAX_ITEMS) {
        snprintf(state->message,sizeof(state->message),"Download limit reached. Delete a saved item first.");
        return;
    }

    jfin_stream_t stream;
    bool ok_url;
    if (item_is_video(item)) {
        vp_3d_mode_t mode_3d = item_3d_mode(item);
        ok_url = jfin_get_video_stream(session, item->id, 0,
                                       mode_3d != VP_3D_NONE, &stream);
    } else {
        ok_url = jfin_get_audio_stream(session, item->id, 0, &stream);
    }
    if (!ok_url) return;

    dl_ctx_t dl={0};dl.item=item;
    int kbps=item_is_video(item)?g_config.video_bitrate+g_config.audio_bitrate:g_config.audio_bitrate;
    if(item->runtime_ticks>0 && kbps>0)dl.est_total=item->runtime_ticks/10000000LL*kbps*1000/8;
    if(!aptMainLoop()){ui_begin_shutdown();return;}
    dl_render_progress(&dl,0);
    bool saved_ok=download_transfer(session,&details,stream.url,dl_pump,&dl);
    snprintf(state->message,sizeof(state->message),"%s",saved_ok?"Saved to SD card. Open Downloads to play offline.":
        dl.cancelled?"Download cancelled.":"Download failed. Check SD space and connection.");
    reload_downloads(state,session);
}

/* ── Selection style helper ────────────────────────────────────────── */

static void draw_list_item_bg(float y, float w, float h, bool selected)
{
    u32 bg = selected ? rgba(COLOR_HIGHLIGHT) : rgba(COLOR_BG_CARD);
    draw_rect(5, y, w, h, bg);
    if (selected)
        draw_rect(5, y, 3, h, rgba(COLOR_PRIMARY));  /* left accent bar */
}

/* ── Settings items ───────────────────────────────────────────────── */

enum {
    SET_AUDIO_BITRATE,
    SET_VIDEO_BITRATE,
    SET_AUTO_ADVANCE,
    SET_CACHE_CLEAR,         /* action: clear offline cache */
    SET_SEPARATOR_ACCOUNT,   /* non-selectable divider */
    SET_SERVER,              /* display-only */
    SET_USERNAME,            /* display-only */
    SET_LOGOUT,              /* action */
    SET_SEPARATOR_ABOUT,     /* non-selectable divider */
    SET_UPDATE,             /* check, then install a verified release */
    SET_VERSION,             /* display-only */
    SET_DEVICE_ID,           /* display-only */
    SET_COUNT
};

static update_info_t s_update;
static bool s_update_installed;
static bool update_ui_progress(uint64_t bytes,uint64_t total,void *context) {
    (void)context;(void)bytes;(void)total;
    if(!aptMainLoop()){audio_player_request_stop();video_player_request_stop();ui_begin_shutdown();return false;}
    hidScanInput();if(hidKeysDown()&KEY_B)return false;
    draw_wait_frame(NULL);return !s_ui_exiting;
}
static void ui_update_app(ui_state_t *state) {
    if(s_update_installed){snprintf(state->message,sizeof(state->message),"Update installed. Exit and reopen the app.");return;}
    if(s_update.available){
        audio_player_stop();video_player_stop();state->has_now_playing=false;state->auto_stopped=true;
        s_wait_label="Installing update... B cancels";draw_wait_frame((void *)1);
        if(!s_ui_exiting)s_update_installed=update_install(&s_update,update_ui_progress,NULL,state->message,sizeof(state->message));
        if(s_update_installed)s_update.available=false;
    }else{
        s_wait_label="Checking updates... B cancels";draw_wait_frame((void *)1);
        if(!s_ui_exiting)update_check(&s_update,update_ui_progress,NULL,state->message,sizeof(state->message));
    }
    s_wait_label="Loading media...";
}
static const int audio_rates[] = {64, 128, 192, 256};
static const int video_rates[] = {256, 472, 768};
#define AUDIO_RATES_COUNT (int)(sizeof(audio_rates) / sizeof(audio_rates[0]))
#define VIDEO_RATES_COUNT (int)(sizeof(video_rates) / sizeof(video_rates[0]))

static bool settings_is_separator(int idx)
{
    return idx == SET_SEPARATOR_ACCOUNT || idx == SET_SEPARATOR_ABOUT;
}

static int settings_next_selectable(int from, int dir)
{
    int n = from + dir;
    while (n >= 0 && n < SET_COUNT && settings_is_separator(n))
        n += dir;
    if (n < 0 || n >= SET_COUNT) return from;
    return n;
}

/* ── Lifecycle ─────────────────────────────────────────────────────── */

bool ui_init(void)
{
    s_ui_exiting=false;memset(&s_update,0,sizeof(s_update));s_update_installed=false;
    jfin_set_wait_callback(draw_wait_frame, NULL);
    s_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_top_right = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    if (!s_top || !s_top_right || !s_bottom) return false;

    s_text_buf = C2D_TextBufNew(4096);
    s_measure_buf = C2D_TextBufNew(256);
    if (!s_text_buf || !s_measure_buf) return false;

    /* Use system font */
    s_font = NULL; /* NULL = default system font */

    return album_art_init();
}

void ui_begin_shutdown(void)
{
    s_ui_exiting=true;
    jfin_set_wait_callback(NULL,NULL);
    jfin_cancel_requests();
    album_art_request_stop();
}
void ui_cleanup(void)
{
    ui_begin_shutdown();
    /* Wait for already-submitted work; do not begin a frame after APT exit. */
    C3D_FrameSync();
    album_art_cleanup();
    if (s_measure_buf) { C2D_TextBufDelete(s_measure_buf); s_measure_buf = NULL; }
    if (s_text_buf) {
        C2D_TextBufDelete(s_text_buf);
        s_text_buf = NULL;
    }
}

static int draw_wrapped(float x, float y, float size, float width,
                         int max_lines, int skip, const char *text);
static int draw_wrapped_color(float x, float y, float size, float width,
                         int max_lines, int skip, const char *text, u32 color);
static int draw_wrapped_depth(float x, float y, float size, float width,
                         int max_lines, int skip, const char *text, u32 color, float depth);

#define POSTER_COLUMNS 3
#define POSTER_STEP 100
#define POSTER_X 10
#define POSTER_Y 50
#define SHELF_HEIGHT 146
#define POSTER_W 90
#define POSTER_H 112
#define NAV_Y 208
static const char *s_shelf_names[] = {"Continue Watching", "Movies", "TV Shows", "Music", "Recently Added"};

static int clamp_index(int index, int count)
{
    if (count <= 0) return 0;
    if (index < 0) return 0;
    return index >= count ? count - 1 : index;
}

static const jfin_item_t *selected_item(const ui_state_t *state)
{
    if (state->current_view == VIEW_HOME) {
        int row = state->home_row;
        int i = state->home_selected[row];
        return i >= 0 && i < state->home_rows[row].count ? &state->home_rows[row].items[i] : NULL;
    }
    if (state->current_view == VIEW_DETAILS) return &state->details.item;
    if (state->current_view == VIEW_DOWNLOADS) return state->download_count>0?
        &state->downloads[state->download_selected].details.item:NULL;
    int i = state->selected_index;
    return i >= 0 && i < state->items.count ? &state->items.items[i] : NULL;
}

static void keep_selection_visible(ui_state_t *state)
{
    if (state->current_view == VIEW_HOME) {
        int row = state->home_row;
        int *index = &state->home_selected[row], *offset = &state->home_offset[row];
        *index = clamp_index(*index, state->home_rows[row].count);
        if (*index < *offset) *offset = *index;
        if (*index >= *offset + POSTER_COLUMNS) *offset = *index - POSTER_COLUMNS + 1;
        state->home_scroll = state->home_row;
    } else {
        state->selected_index = clamp_index(state->selected_index, state->items.count);
        state->scroll_offset = (state->selected_index / POSTER_COLUMNS) * POSTER_COLUMNS;
    }
}

static void poster_input(ui_state_t *state, u32 kdown, u32 kheld, touchPosition touch)
{
    static int repeat, last_view = -1;
    u32 direction = kdown & (KEY_DLEFT | KEY_DRIGHT | KEY_DUP | KEY_DDOWN);
    if (last_view != (int)state->current_view || direction) repeat = 0;
    last_view = state->current_view;
    if (!direction && (kheld & (KEY_DLEFT | KEY_DRIGHT | KEY_DUP | KEY_DDOWN))) {
        repeat++;
        if (repeat >= 12 && repeat % 4 == 0) direction = kheld;
    } else if (!direction) repeat = 0;
    bool home = state->current_view == VIEW_HOME;
    if (home) {
        if ((direction & KEY_DUP) && state->home_row > 0) state->home_row--;
        if ((direction & KEY_DDOWN) && state->home_row < 4) state->home_row++;
        if (direction & KEY_DLEFT) state->home_selected[state->home_row]--;
        if (direction & KEY_DRIGHT) state->home_selected[state->home_row]++;
    } else {
        if (direction & KEY_DLEFT) state->selected_index--;
        if (direction & KEY_DRIGHT) state->selected_index++;
        if (direction & KEY_DUP) state->selected_index -= POSTER_COLUMNS;
        if (direction & KEY_DDOWN) state->selected_index += POSTER_COLUMNS;
    }
    if ((kdown & KEY_TOUCH) && touch.py >= 32 && touch.py < NAV_Y) {
        state->touch_held = true; state->touch_dragged = false;
        state->touch_start_x = state->touch_anchor_x = touch.px;
        state->touch_start_y = state->touch_anchor_y = touch.py;
    }
    if (state->touch_held && (kheld & KEY_TOUCH)) {
        int dx = state->touch_anchor_x - touch.px;
        int dy = state->touch_anchor_y - touch.py;
        if (dx > 14 || dx < -14 || dy > 14 || dy < -14) state->touch_dragged = true;
        if (dx >= POSTER_STEP || dx <= -POSTER_STEP) {
            int steps = dx / POSTER_STEP;
            if (home) {
                int row = state->home_scroll + (state->touch_start_y - 32) / SHELF_HEIGHT;
                if (row >= 0 && row < 5) { state->home_row = row; state->home_selected[row] += steps; }
            } else state->selected_index += steps * POSTER_COLUMNS;
            state->touch_anchor_x = touch.px;
        } else if (dy >= 50 || dy <= -50) {
            if (home) state->home_row = clamp_index(state->home_row + (dy > 0 ? 1 : -1), 5);
            else state->selected_index += dy > 0 ? POSTER_COLUMNS : -POSTER_COLUMNS;
            state->touch_anchor_y = touch.py;
        }
    }
    if (state->touch_held && !(kheld & KEY_TOUCH)) {
        if (!state->touch_dragged) {
            int relative_row = (state->touch_start_y - 32) / SHELF_HEIGHT;
            int col = (state->touch_start_x - POSTER_X) / POSTER_STEP;
            int tile_y = state->touch_start_y - (POSTER_Y + relative_row * SHELF_HEIGHT);
            int tile_x = state->touch_start_x - (POSTER_X + col * POSTER_STEP);
            if (col >= 0 && col < POSTER_COLUMNS && tile_x >= 0 && tile_x < POSTER_W && tile_y >= 0 && tile_y < POSTER_H + 14) {
                if (home) {
                    int row = state->home_scroll + relative_row;
                    if (row < 5 && state->home_offset[row] + col < state->home_rows[row].count) {
                        state->home_row = row;
                        state->home_selected[row] = state->home_offset[row] + col;
                    }
                } else {
                    int i = state->scroll_offset + relative_row * POSTER_COLUMNS + col;
                    if (i < state->items.count) state->selected_index = i;
                }
            }
        }
        state->touch_held = false;
    }
    keep_selection_visible(state);
}

static void open_details(ui_state_t *state, const jfin_session_t *session,
                          const jfin_item_t *item)
{
    state->details_return_view = state->current_view;
    if (state->preview_ready && !strcmp(state->preview.item.id, item->id)) {
        state->details = state->preview; state->details_loading = false;
    } else {
        memset(&state->details, 0, sizeof(state->details));
        state->details.item = *item; state->details_loading = true;
        album_art_request_details(session, item->id);
    }
    state->details_scroll = 0; state->touch_held = false;
    state->current_view = VIEW_DETAILS;
}

static void update_preview(ui_state_t *state, const jfin_session_t *session)
{
    if (state->current_view != VIEW_HOME && state->current_view != VIEW_BROWSE &&
        state->current_view != VIEW_LIBRARIES && state->current_view != VIEW_DETAILS) return;
    const jfin_item_t *item = selected_item(state);
    if (!item) return;
    if (strcmp(state->preview.item.id, item->id)) {
        memset(&state->preview, 0, sizeof(state->preview));
        state->preview.item = *item; state->preview_ready = false; state->preview_failed = false; state->preview_frames = 0;
    }
    unsigned delay = state->preview_failed ? 120 : 8;
    if (state->preview_frames < delay) state->preview_frames++;
    if (state->preview_frames == delay && !state->preview_ready)
        album_art_request_details(session, item->id);
    bool success;
    if (album_art_take_details(item->id, &state->preview, &success)) {
        state->preview_ready = success; state->preview_failed = !success;
        if (!success) state->preview_frames = 0;
        if (state->current_view == VIEW_DETAILS) {
            if (success) state->details = state->preview;
            state->details_loading = !success;
        }
    }
}

/* ── Input Handling ────────────────────────────────────────────────── */

void ui_update(ui_state_t *state, const jfin_session_t *session,
               u32 kdown, u32 kheld, touchPosition touch)
{
    if (kdown && state->message[0]) state->message[0] = '\0';
    if ((kdown & KEY_TOUCH) && touch.py < 25 && touch.px >= 240 &&
        (state->current_view == VIEW_HOME || state->current_view == VIEW_BROWSE ||
         state->current_view == VIEW_LIBRARIES)) kdown |= KEY_Y;
    if ((kdown & KEY_TOUCH) && state->current_view == VIEW_DETAILS && touch.py >= 43 && touch.py < 80) {
        if (touch.px >= 10 && touch.px < 155) kdown |= KEY_A;
        if (touch.px >= 166 && touch.px < 310) kdown |= KEY_X;
    }
    if ((kdown & KEY_TOUCH) && state->current_view==VIEW_DETAILS && touch.py>=90 && touch.py<125)kdown|=KEY_Y;
    if ((kdown & KEY_TOUCH) && touch.py >= NAV_Y && state->current_view != VIEW_LOGIN &&
        state->current_view != VIEW_NOW_PLAYING) {
        state->touch_held = false;
        if (state->current_view == VIEW_SETTINGS) config_save(&g_config);
        int tab = touch.px / 80;
        if (tab == 0) state->current_view = VIEW_HOME;
        else if (tab == 1) {
            if (state->current_view != VIEW_BROWSE && state->current_view != VIEW_LIBRARIES &&
                jfin_get_views(session, &state->items)) {
                state->selected_index = state->scroll_offset = state->parent_depth = 0;
                state->current_view = VIEW_LIBRARIES;
            }
        } else if (tab == 2 && state->current_view != VIEW_DOWNLOADS) {
            state->downloads_return_view = state->current_view;
            reload_downloads(state,session);
            state->current_view = VIEW_DOWNLOADS;
        } else if (tab == 3 && state->current_view != VIEW_SETTINGS) {
            state->previous_view = state->current_view;
            state->settings_index = state->settings_scroll = 0;
            s_cache_bytes_ui = cache_total_bytes(); state->current_view = VIEW_SETTINGS;
        }
        return;
    }
    if (state->seek_pause_pending) {
        video_status_t vs = video_player_get_status();
        player_status_t ps = audio_player_get_status();
        if (vs.state == VIDEO_PLAYING) {
            video_player_pause();
            state->seek_pause_pending = false;
        } else if (ps.state == PLAYER_PLAYING) {
            audio_player_pause();
            state->seek_pause_pending = false;
        } else if (vs.state != VIDEO_LOADING && ps.state != PLAYER_LOADING) {
            state->seek_pause_pending = false;
        }
    }

    switch (state->current_view) {
    case VIEW_LOGIN:
        /* D-pad up/down to select field */
        if (kdown & KEY_DUP) {
            state->login_field = (state->login_field + 2) % 3;
        }
        if (kdown & KEY_DDOWN) {
            state->login_field = (state->login_field + 1) % 3;
        }
        /* A to activate swkbd for the selected field */
        if (kdown & KEY_A) {
            SwkbdState swkbd;
            char buf[JFIN_MAX_URL] = {0};

            SwkbdType type = (state->login_field == 2)
                ? SWKBD_TYPE_WESTERN : SWKBD_TYPE_WESTERN;
            swkbdInit(&swkbd, type, 2, state->login_field == 0 ? 1023 : 63);

            switch (state->login_field) {
            case 0:
                swkbdSetHintText(&swkbd, "Server URL (e.g. https://media.example.com)");
                snprintf(buf, sizeof(buf), "%s", state->server_url);
                break;
            case 1:
                swkbdSetHintText(&swkbd, "Username");
                snprintf(buf, sizeof(buf), "%s", state->username);
                break;
            case 2:
                swkbdSetHintText(&swkbd, "Password");
                swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
                break;
            }

            swkbdSetInitialText(&swkbd, buf);
            SwkbdButton button = swkbdInputText(&swkbd, buf, sizeof(buf));

            if (button == SWKBD_BUTTON_CONFIRM) {
                switch (state->login_field) {
                case 0: snprintf(state->server_url, sizeof(state->server_url), "%s", buf); break;
                case 1: snprintf(state->username, sizeof(state->username), "%s", buf); break;
                case 2: snprintf(state->password, sizeof(state->password), "%s", buf); break;
                }
            }
        }
        /* START to attempt login */
        if (kdown & KEY_R) {
            /* Show connecting indicator before blocking login call */
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TextBufClear(s_text_buf);
            C2D_TargetClear(s_bottom, rgba(COLOR_BG_DARK));
            C2D_SceneBegin(s_bottom);
            draw_text(100, 110, 0.6f, rgba(COLOR_PRIMARY), "Connecting...");
            C3D_FrameEnd(0);

            jfin_session_t *s = (jfin_session_t *)session; /* cast away const for login */
            if (jfin_login(s, state->server_url, state->username, state->password,
                           g_config.device_id)) {
                /* Save credentials immediately so they persist even on crash */
                bool saved = config_save_session(&g_config, s->server_url,
                                    s->access_token, s->user_id,
                                    state->username);
                snprintf(state->message, sizeof(state->message), "%s",
                         saved ? "" : "Login works, but saving to SD failed.");
                /* Token obtained — no reason to keep the password in RAM */
                memset(state->password, 0, sizeof(state->password));
                state->current_view = VIEW_HOME;
                state->home_requested = false;
                state->preview.item.id[0] = '\0';
                jfin_get_views(session, &state->items);
                state->selected_index = 0;
                state->scroll_offset = 0;
            } else {
                snprintf(state->message, sizeof(state->message), "%s", jfin_last_error());
            }
        }
        break;

    case VIEW_HOME:
        if (!state->home_requested) {
            memset(state->home_loaded, 0, sizeof(state->home_loaded));
            album_art_request_home(session); state->home_requested = true;
        }
        for (int row = 0; row < 5; row++) {
            if (album_art_take_home(row, &state->home_rows[row], &state->home_ok[row]))
                state->home_loaded[row] = true;
        }
        poster_input(state, kdown, kheld, touch);
        if (kdown & KEY_L) { state->home_requested = false; state->preview.item.id[0] = '\0'; }
        if (kdown & KEY_A) {
            const jfin_item_t *item = selected_item(state);
            if (item) {
                /* Preserve the shelf as the playback sequence; home rows stay separate. */
                state->items = state->home_rows[state->home_row];
                state->selected_index = state->home_selected[state->home_row];
                open_details(state, session, item);
            }
        }
        if (kdown & KEY_X) {
            if (jfin_get_views(session, &state->items)) {
                state->selected_index = state->scroll_offset = state->parent_depth = 0;
                state->current_view = VIEW_LIBRARIES;
            }
        }
        if ((kdown & (KEY_B | KEY_ZR)) && state->has_now_playing) {
            state->previous_view = VIEW_HOME; state->current_view = VIEW_NOW_PLAYING;
        }
        if (kdown & KEY_Y) {
            SwkbdState keyboard; char query[128] = {0};
            swkbdInit(&keyboard, SWKBD_TYPE_WESTERN, 2, 127);
            swkbdSetHintText(&keyboard, "Search Jellyfin");
            if (swkbdInputText(&keyboard, query, sizeof(query)) == SWKBD_BUTTON_CONFIRM && query[0] &&
                jfin_search(session, query, JFIN_MAX_ITEMS, &state->items)) {
                state->selected_index = state->scroll_offset = state->parent_depth = 0;
                state->browse_root_view = VIEW_HOME; state->current_view = VIEW_BROWSE;
            }
        }
        if (kdown & KEY_SELECT) {
            state->previous_view = VIEW_HOME;
            state->settings_index = state->settings_scroll = 0;
            s_cache_bytes_ui = cache_total_bytes(); state->current_view = VIEW_SETTINGS;
        }
        break;

    case VIEW_LIBRARIES:
    case VIEW_BROWSE:
        poster_input(state, kdown, kheld, touch);
        /* A to enter / play */
        if (kdown & KEY_A) {
            if (state->selected_index < state->items.count) {
                jfin_item_t *item = &state->items.items[state->selected_index];
                bool is_playable = (item->type == JFIN_ITEM_AUDIO ||
                                    item->type == JFIN_ITEM_MOVIE ||
                                    item->type == JFIN_ITEM_EPISODE);
                bool is_container = (item->type == JFIN_ITEM_FOLDER ||
                                     item->type == JFIN_ITEM_MUSIC_ALBUM ||
                                     item->type == JFIN_ITEM_MUSIC_ARTIST ||
                                     item->type == JFIN_ITEM_SERIES ||
                                     item->type == JFIN_ITEM_SEASON);
                if (is_playable || (is_container && item->type != JFIN_ITEM_FOLDER)) {
                    open_details(state, session, item);
                } else if (is_container) {
                    ui_navigate_into(state, session, item);
                }
                /* JFIN_ITEM_UNKNOWN: do nothing */
            }
        }
        /* B to go back */
        if (kdown & KEY_B) {
            ui_navigate_back(state, session);
        }
        /* X: download to / remove from the offline cache */
        if ((kdown & KEY_X) && state->selected_index < state->items.count) {
            jfin_item_t *item = &state->items.items[state->selected_index];
            /* Video on Old 3DS can't be played, so don't cache it either */
            bool cacheable = item_is_playable(item) &&
                             (!item_is_video(item) || video_player_is_supported());
            if (cacheable) {
                ui_download_item(state,session, item);
            }
        }
        /* L/R for pagination */
        if (kdown & KEY_R) {
            /* Next page */
            if (state->items.start_index + state->items.count < state->items.total_count) {
                int next_start = state->items.start_index + JFIN_MAX_ITEMS;
                const char *parent_id = (state->parent_depth > 0)
                    ? state->parent_stack_ids[state->parent_depth - 1] : NULL;
                if (state->current_view == VIEW_LIBRARIES) {
                    /* Libraries don't paginate the same way */
                } else if (parent_id) {
                    jfin_get_items(session, parent_id, next_start, JFIN_MAX_ITEMS, &state->items);
                    state->selected_index = 0;
                    state->scroll_offset = 0;
                }
            }
        }
        if (kdown & KEY_L) {
            /* Previous page */
            if (state->items.start_index > 0) {
                int prev_start = state->items.start_index - JFIN_MAX_ITEMS;
                if (prev_start < 0) prev_start = 0;
                const char *parent_id = (state->parent_depth > 0)
                    ? state->parent_stack_ids[state->parent_depth - 1] : NULL;
                if (parent_id) {
                    jfin_get_items(session, parent_id, prev_start, JFIN_MAX_ITEMS, &state->items);
                    state->selected_index = 0;
                    state->scroll_offset = 0;
                }
            }
        }
        if ((kdown & KEY_ZR) && state->has_now_playing) {
            state->previous_view = state->current_view; state->current_view = VIEW_NOW_PLAYING;
        }
        if (kdown & KEY_Y) {
            SwkbdState keyboard; char query[128] = {0};
            swkbdInit(&keyboard, SWKBD_TYPE_WESTERN, 2, 127);
            swkbdSetHintText(&keyboard, "Search Jellyfin");
            if (swkbdInputText(&keyboard, query, sizeof(query)) == SWKBD_BUTTON_CONFIRM && query[0]) {
                if (jfin_search(session, query, JFIN_MAX_ITEMS, &state->items)) {
                    state->selected_index = state->scroll_offset = state->parent_depth = 0;
                    state->browse_root_view = VIEW_HOME;
                    state->current_view = VIEW_BROWSE;
                }
            }
        }
        if (kdown & KEY_SELECT) {
            state->previous_view = state->current_view;
            state->settings_index = state->settings_scroll = 0;
            s_cache_bytes_ui = cache_total_bytes(); state->current_view = VIEW_SETTINGS;
        }
        break;

    case VIEW_NOW_PLAYING:
        {
            video_status_t vs = video_player_get_status();
            bool vid_active = (vs.state == VIDEO_PLAYING || vs.state == VIDEO_PAUSED ||
                               vs.state == VIDEO_LOADING);

            /* Error state: any button press stops and returns to browse */
            if (vs.state == VIDEO_ERROR) {
                if (kdown) {
                    video_player_stop();
                    audio_player_stop();
                    state->has_now_playing = false;
                    state->current_view = state->previous_view;
                }
                break;
            }

            /* Watch mode: D-pad down hides controls, only up exits */
            if (state->bottom_hidden) {
                if (kdown & KEY_DUP)
                    state->bottom_hidden = false;
                break; /* ignore all other buttons in watch mode */
            }
            /* D-pad down: enter watch mode */
            if (kdown & KEY_DDOWN) {
                state->seeking = false;
                state->bottom_hidden = true;
                break;
            }
            /* A to pause/resume */
            if (kdown & KEY_A) {
                if (vid_active)
                    video_player_pause();
                else
                    audio_player_pause();
            }
            int64_t duration = state->now_playing.runtime_ticks;
            if ((kdown & KEY_TOUCH) && state->has_now_playing &&
                timeline_hit(touch.px, touch.py, duration)) {
                state->seeking = true;
                state->seek_was_paused = vid_active ? vs.state == VIDEO_PAUSED :
                    audio_player_get_status().state == PLAYER_PAUSED;
                state->seek_preview_ticks = timeline_position(touch.px, duration);
            }
            if (state->seeking) {
                if (kdown & (KEY_B | KEY_X)) {
                    state->seeking = false; /* cancel without restarting */
                } else if (kheld & KEY_TOUCH) {
                    state->seek_preview_ticks = timeline_position(touch.px, duration);
                } else {
                    ui_seek(state, session, state->seek_preview_ticks, state->seek_was_paused);
                    break; /* old player status is stale after restart */
                }
            }
            /* Shoulder seeking shares exactly the same clamp and restart. */
            if (!state->seeking && duration > 0 && (kdown & (KEY_L | KEY_R))) {
                int64_t cur_pos = vid_active ? vs.position_ticks :
                    audio_player_get_status().position_ticks;
                cur_pos = timeline_clamp(cur_pos, duration);
                int64_t offset = (kdown & KEY_R) ? 300000000LL : -300000000LL;
                int64_t new_pos = offset > 0 && cur_pos > INT64_MAX - offset ?
                    duration : cur_pos + offset;
                bool paused = vid_active ? vs.state == VIDEO_PAUSED :
                    audio_player_get_status().state == PLAYER_PAUSED;
                ui_seek(state, session, new_pos, paused);
                break;
            }
            /* B to go back to browse */
            if (kdown & KEY_B) {
                state->bottom_hidden = false;
                state->current_view = state->previous_view;
            }
            /* X to stop */
            if (kdown & KEY_X) {
                state->bottom_hidden = false;
                video_player_stop();
                audio_player_stop();
                state->has_now_playing = false;
                state->auto_stopped = true;
                state->current_view = state->previous_view;
            }
        }
        break;

    case VIEW_DETAILS:
        if((kdown & KEY_Y) && item_is_playable(&state->details.item))ui_download_item(state,session,&state->details.item);
        if (kdown & KEY_DUP) {
            if (state->details_scroll > 0) state->details_scroll--;
        }
        if (kdown & KEY_DDOWN) {
            int lines = draw_wrapped(0, 0, 0.40f, 228, 0, 0, state->details.overview);
            if (state->details_scroll + (state->details.resume_ticks > 0 ? 3 : 4) < lines) state->details_scroll++;
        }
        if (kdown & KEY_B) state->current_view = state->details_return_view;
        if ((kdown & KEY_ZR) && state->has_now_playing) {
            state->previous_view = VIEW_DETAILS; state->current_view = VIEW_NOW_PLAYING;
        }
        if (kdown & (KEY_A | KEY_X)) {
            jfin_item_t *item = &state->details.item;
            if (item_is_playable(item)) {
                if ((kdown & KEY_A) && state->details_loading) {
                    snprintf(state->message, sizeof(state->message), "Loading resume position..."); break;
                }
                int64_t start = (kdown & KEY_X) ? 0 :
                    timeline_clamp(state->details.resume_ticks, item->runtime_ticks);
                if (ui_start_item(state, session, item, state->selected_index, start)) {
                    state->previous_view = VIEW_DETAILS;
                    state->current_view = VIEW_NOW_PLAYING;
                } else {
                    snprintf(state->message, sizeof(state->message), "Could not start playback.");
                }
            } else if (kdown & KEY_A) {
                ui_navigate_into(state, session, item);
            }
        }
        break;

    case VIEW_DOWNLOADS:
        if(kdown)state->message[0]=0;
        if(kdown & KEY_B)state->current_view=state->downloads_return_view;
        if((kdown & KEY_DUP) && state->download_selected>0)state->download_selected--;
        if((kdown & KEY_DDOWN) && state->download_selected+1<state->download_count)state->download_selected++;
        if((kdown & KEY_TOUCH) && touch.py>=35 && touch.py<158) {
            int i=state->download_scroll+(touch.py-35)/41;
            if(i<state->download_count)state->download_selected=i;
        }
        if((kdown & KEY_TOUCH) && touch.py>=160 && touch.py<198)kdown|=touch.px<160?KEY_A:KEY_X;
        if(state->download_selected<state->download_scroll)state->download_scroll=state->download_selected;
        if(state->download_selected>=state->download_scroll+3)state->download_scroll=state->download_selected-2;
        if(state->download_count>0) {
            download_t *saved=&state->downloads[state->download_selected];
            if(kdown & KEY_A) {
                if(ui_start_item(state,session,&saved->details.item,0,0)) {
                    state->previous_view=VIEW_DOWNLOADS;state->current_view=VIEW_NOW_PLAYING;
                } else snprintf(state->message,sizeof(state->message),"Could not play saved file. Streaming was not used.");
            }
            if(kdown & KEY_X) {
                if(state->has_now_playing && !strcmp(state->now_playing.id,saved->details.item.id)) {
                    video_player_stop();audio_player_stop();state->has_now_playing=false;state->auto_stopped=true;
                }
                bool removed=download_delete(saved);reload_downloads(state,session);
                snprintf(state->message,sizeof(state->message),"%s",removed?"Download deleted.":"Could not delete download.");
            }
        }
        break;

    case VIEW_SETTINGS:
        if((kdown&KEY_TOUCH)&&touch.py>=30&&touch.py<190){
            int index=state->settings_scroll+(touch.py-30)/UI_LIST_ITEM_HEIGHT;
            if(index<SET_COUNT&&!settings_is_separator(index)){state->settings_index=index;kdown|=KEY_A;}
        }
        /* D-pad up/down: move cursor, skip separators */
        if (kdown & KEY_DUP)
            state->settings_index = settings_next_selectable(state->settings_index, -1);
        if (kdown & KEY_DDOWN)
            state->settings_index = settings_next_selectable(state->settings_index, 1);

        /* Scroll to keep cursor visible */
        if (state->settings_index < state->settings_scroll)
            state->settings_scroll = state->settings_index;
        if (state->settings_index >= state->settings_scroll + UI_MAX_VISIBLE_ITEMS)
            state->settings_scroll = state->settings_index - UI_MAX_VISIBLE_ITEMS + 1;

        /* L/R: cycle selector values */
        if ((kdown & KEY_DLEFT) || (kdown & KEY_DRIGHT) ||
            (kdown & KEY_L) || (kdown & KEY_R)) {
            int dir = ((kdown & KEY_DRIGHT) || (kdown & KEY_R)) ? 1 : -1;
            if (state->settings_index == SET_AUDIO_BITRATE) {
                int cur = 0;
                for (int i = 0; i < AUDIO_RATES_COUNT; i++)
                    if (g_config.audio_bitrate == audio_rates[i]) { cur = i; break; }
                cur = (cur + dir + AUDIO_RATES_COUNT) % AUDIO_RATES_COUNT;
                g_config.audio_bitrate = audio_rates[cur];
            } else if (state->settings_index == SET_VIDEO_BITRATE) {
                int cur = 0;
                for (int i = 0; i < VIDEO_RATES_COUNT; i++)
                    if (g_config.video_bitrate == video_rates[i]) { cur = i; break; }
                cur = (cur + dir + VIDEO_RATES_COUNT) % VIDEO_RATES_COUNT;
                g_config.video_bitrate = video_rates[cur];
            }
        }

        /* A: toggle booleans, activate actions */
        if (kdown & KEY_A) {
            if (state->settings_index == SET_AUTO_ADVANCE) {
                g_config.auto_advance = !g_config.auto_advance;
                state->auto_advance = g_config.auto_advance;
            } else if (state->settings_index == SET_CACHE_CLEAR) {
                /* Stop any active playback first: a cached track/video may
                 * be open via the sdmc devoptab, and deleting an open file
                 * underneath FFmpeg/mpg123 is undefined on FAT. */
                audio_player_stop();
                video_player_stop();
                state->has_now_playing = false;
                cache_clear();
                s_cache_bytes_ui = 0;
            } else if (state->settings_index == SET_UPDATE) {
                ui_update_app(state);
            } else if (state->settings_index == SET_LOGOUT) {
                jfin_session_t *s = (jfin_session_t *)session;
                audio_player_stop(); video_player_stop();
                state->has_now_playing = false; state->home_requested = false;
                state->preview.item.id[0] = '\0';
                jfin_logout(s);
                g_config.access_token[0] = '\0';
                config_save(&g_config);
                state->current_view = VIEW_LOGIN;
                if (g_config.server_url[0] != '\0')
                    snprintf(state->server_url, sizeof(state->server_url),
                             "%s", g_config.server_url);
            }
        }

        /* B: save and restore the browsing screen. */
        if (kdown & KEY_B) {
            config_save(&g_config);
            state->current_view = state->previous_view;
        }
        break;
    }
    /* Browsing home or metadata must not change the active playback sequence. */
    if (state->has_now_playing && !state->playback_offline_only && state->auto_advance && !state->auto_stopped && !state->seeking &&
        audio_player_get_status().state == PLAYER_STOPPED && video_player_get_status().state == VIDEO_STOPPED) {
        int next = state->playing_index + 1;
        bool advanced = next < state->play_queue.count && item_is_playable(&state->play_queue.items[next]) &&
            ui_start_item(state, session, &state->play_queue.items[next], next, 0);
        if (!advanced) {
            state->has_now_playing = false;
            if (state->current_view == VIEW_NOW_PLAYING) state->current_view = state->previous_view;
        }
    }
    update_preview(state, session);
}

/* ── Navigation ────────────────────────────────────────────────────── */

void ui_navigate_into(ui_state_t *state, const jfin_session_t *session,
                      const jfin_item_t *item)
{
    if (state->parent_depth == 0) {
        state->browse_root_view = state->current_view == VIEW_LIBRARIES ? VIEW_LIBRARIES :
            (state->current_view == VIEW_DETAILS ? state->details_return_view : VIEW_HOME);
    }
    if (state->parent_depth >= 8) { snprintf(state->message, sizeof(state->message), "Folder nesting limit reached."); return; }
    state->touch_held = false;
    /* Push breadcrumb first, then fetch. If empty, pop it back. */
    char saved_id[JFIN_MAX_ID];
    char saved_name[JFIN_MAX_NAME];
    snprintf(saved_id, sizeof(saved_id), "%s", item->id);
    snprintf(saved_name, sizeof(saved_name), "%s", item->name);

    if (state->parent_depth < 8) {
        snprintf(state->parent_stack_ids[state->parent_depth],
                 sizeof(state->parent_stack_ids[0]), "%s", saved_id);
        snprintf(state->parent_stack_names[state->parent_depth],
                 sizeof(state->parent_stack_names[0]), "%s", saved_name);
        state->parent_depth++;
    }

    state->current_view = VIEW_BROWSE;
    state->selected_index = 0;
    state->scroll_offset = 0;

    draw_wait_frame((void *)1);

    /* Fetch into state->items directly (no stack-heavy temp copy) */
    jfin_get_items(session, saved_id, 0, JFIN_MAX_ITEMS, &state->items);

    log_write("NAV: into '%s' id=%s depth=%d items=%d",
              saved_name, saved_id, state->parent_depth, state->items.count);

    if (state->items.count == 0) {
        /* Empty folder — undo navigation */
        log_write("NAV: empty, undoing");
        ui_navigate_back(state, session);
    }
}

void ui_navigate_back(ui_state_t *state, const jfin_session_t *session)
{
    state->touch_held = false;
    if (state->current_view == VIEW_LIBRARIES ||
        (state->parent_depth <= 0 && state->browse_root_view == VIEW_HOME)) {
        state->current_view = VIEW_HOME; return;
    }
    if (state->parent_depth <= 0) {
        /* Already at top — go to libraries */
        state->current_view = VIEW_LIBRARIES;
        jfin_get_views(session, &state->items);
        state->selected_index = 0;
        state->scroll_offset = 0;
        return;
    }

    state->parent_depth--;
    state->selected_index = 0;
    state->scroll_offset = 0;

    if (state->parent_depth == 0) {
        if (state->browse_root_view == VIEW_HOME) { state->current_view = VIEW_HOME; return; }
        state->current_view = VIEW_LIBRARIES;
        jfin_get_views(session, &state->items);
    } else {
        const char *parent_id = state->parent_stack_ids[state->parent_depth - 1];
        jfin_get_items(session, parent_id, 0, JFIN_MAX_ITEMS, &state->items);
    }
}

/* ── Renderers ─────────────────────────────────────────────────────── */

void ui_render_login(const ui_state_t *state)
{
    /* Bottom screen: login form */
    C2D_TargetClear(s_bottom, rgba(COLOR_BG_DARK));
    C2D_SceneBegin(s_bottom);

    draw_text(10, 10, 0.7f, rgba(COLOR_PRIMARY), "Connect to Jellyfin Server");

    const char *labels[] = {"Server URL:", "Username:", "Password:"};
    const char *values[] = {state->server_url, state->username, "********"};

    for (int i = 0; i < 3; i++) {
        float y = 50 + i * 50;
        u32 bg = (i == state->login_field) ? rgba(COLOR_HIGHLIGHT) : rgba(COLOR_BG_CARD);
        draw_rect(10, y, 300, 40, bg);
        draw_text(15, y + 2, 0.45f, rgba(COLOR_TEXT_SECONDARY), labels[i]);
        draw_text(15, y + 18, 0.5f, rgba(COLOR_TEXT_PRIMARY),
                  values[i][0] ? values[i] : "(tap A to enter)");
    }

    draw_text(10, 210, 0.45f, rgba(COLOR_TEXT_SECONDARY),
              "A: Edit field  R: Connect  START: Exit");
}

static const jfin_session_t *s_render_session;

static void draw_poster(const ui_state_t *state, const jfin_item_t *item,
                         float x, float y, float w, float h, bool selected)
{
    if (selected) draw_rect(x - 2, y - 2, w + 4, h + 4, rgba(COLOR_HIGHLIGHT));
    draw_rect(x, y, w, h, rgba(COLOR_BG_CARD));
    const jfin_item_t *art_item = item;
    if (state->preview_ready && !strcmp(state->preview.item.id, item->id))
        art_item = &state->preview.item;
    if(state->current_view==VIEW_DOWNLOADS)album_art_request_cached(s_render_session,art_item);
    else album_art_request(s_render_session, art_item);
    if (!album_art_draw_item(s_render_session, art_item, x, y, w, h)) {
        draw_wrapped(x + 4, y + 8, 0.30f, w - 8, 3, 0, item->name);
        draw_text(x + 4, y + h - 14, 0.27f, rgba(COLOR_TEXT_SECONDARY),
                  item->type == JFIN_ITEM_MUSIC_ALBUM || item->type == JFIN_ITEM_AUDIO ? "MUSIC" :
                  item->type == JFIN_ITEM_SERIES ? "TV" : "JELLYFIN");
    }
}

static void draw_card(float x,float y,float w,float h)
{
    draw_rect(x+2,y,w-4,h,rgba(COLOR_BG_CARD));
    draw_rect(x,y+2,w,h-4,rgba(COLOR_BG_CARD));
}
static void draw_resume_bar(float x,float y,float w,int64_t position,int64_t duration)
{
    if (duration<=0 || position<=0) return;
    double fraction=(double)timeline_clamp(position,duration)/(double)duration;
    C2D_DrawRectSolid(x,y,0.6f,w,4,rgba(COLOR_SEPARATOR));
    C2D_DrawRectSolid(x,y,0.65f,(float)(w*fraction),4,rgba(COLOR_PRIMARY));
}
static void render_selected_card(const ui_state_t *state)
{
    C2D_TargetClear(s_top,rgba(COLOR_BG_DARK));C2D_SceneBegin(s_top);
    draw_rect(0,0,400,26,rgba(COLOR_PRIMARY));
    draw_text(12,3,0.58f,C2D_Color32(255,255,255,255),"JELLYFIN");
    draw_text(320,7,0.30f,C2D_Color32(255,255,255,255),
              s_render_session->authenticated ? "Connected" : "Offline");
    const jfin_item_t *item=selected_item(state);
    if (!item) {
        draw_wrapped_color(24,72,0.60f,350,2,0,"Your Jellyfin library",rgba(COLOR_TEXT_PRIMARY));
        draw_wrapped(24,125,0.42f,350,3,0,"Select a poster below, or open Library.");return;
    }
    const jfin_item_details_t *details=state->current_view==VIEW_DETAILS ? &state->details :
        state->current_view==VIEW_DOWNLOADS?&state->downloads[state->download_selected].details:&state->preview;
    if (state->current_view!=VIEW_DETAILS && state->current_view!=VIEW_DOWNLOADS && state->preview_ready && !strcmp(details->item.id,item->id)) item=&details->item;
    draw_poster(state,item,12,38,120,188,false);
    draw_card(144,38,244,188);
    draw_wrapped_color(154,44,0.53f,224,2,0,item->name,rgba(COLOR_TEXT_PRIMARY));
    char meta[160],duration[24]="";
    if (item->runtime_ticks>0) {
        int minutes=(int)(item->runtime_ticks/600000000LL);
        if (minutes>=60)snprintf(duration,sizeof(duration),"%dh %dm",minutes/60,minutes%60);
        else snprintf(duration,sizeof(duration),"%dm",minutes);
    }
    if(item->year>0)snprintf(meta,sizeof(meta),"%d   %.12s   %s",item->year,details->official_rating,duration);
    else snprintf(meta,sizeof(meta),"%.12s   %s",details->official_rating,duration);
    draw_wrapped(154,84,0.34f,224,1,0,meta);
    if(details->community_rating>0){snprintf(meta,sizeof(meta),"%.1f / 10",details->community_rating);
        draw_text(154,105,0.44f,rgba(COLOR_PRIMARY),meta);}
    if(item->type==JFIN_ITEM_EPISODE)snprintf(meta,sizeof(meta),"%.85s - S%d E%d",item->series_name,item->season_number,item->index_number);
    else if(item->artist[0])snprintf(meta,sizeof(meta),"%.70s / %.70s",item->artist,item->album);
    else snprintf(meta,sizeof(meta),"%.120s",details->genres);
    draw_wrapped(154,127,0.33f,224,1,0,meta);
    bool loading=state->current_view==VIEW_DOWNLOADS?false:state->current_view==VIEW_DETAILS ? state->details_loading : !state->preview_ready;
    const char *overview=details->overview[0] ? details->overview : loading ?
        (state->preview_failed ? "Information failed to load. Retrying..." : "Loading information...") : "No description available.";
    int64_t resume=details->resume_ticks>0 ? details->resume_ticks : item->resume_ticks;
    draw_wrapped(154,149,0.37f,224,resume>0?3:4,state->current_view==VIEW_DETAILS?state->details_scroll:0,overview);
    if(resume>0 && item->runtime_ticks>0){
        char time[32];format_ticks(timeline_clamp(resume,item->runtime_ticks),time,sizeof(time));
        snprintf(meta,sizeof(meta),"Resume at %s",time);draw_text(154,203,0.32f,rgba(COLOR_TEXT_PRIMARY),meta);
        draw_resume_bar(154,220,224,resume,item->runtime_ticks);
    }
    if(loading)draw_loading_ring(373,208);
}
static void browser_header(const char *title)
{
    C2D_TargetClear(s_bottom,rgba(COLOR_BG_DARK));C2D_SceneBegin(s_bottom);
    draw_rect(0,0,320,26,rgba(COLOR_PRIMARY));
    draw_wrapped_color(10,3,0.46f,222,1,0,title,C2D_Color32(255,255,255,255));
    draw_text(244,6,0.34f,C2D_Color32(255,255,255,255),"Y Search");
}
static void draw_shelf_poster(const ui_state_t *state,const jfin_item_t *item,int col,int y,bool selected)
{
    float x=POSTER_X+col*POSTER_STEP;
    draw_poster(state,item,x,y,POSTER_W,POSTER_H,selected);
    draw_resume_bar(x+4,y+POSTER_H-7,POSTER_W-8,item->resume_ticks,item->runtime_ticks);
    draw_wrapped_color(x,y+POSTER_H+4,0.31f,POSTER_W,1,0,item->name,rgba(COLOR_TEXT_PRIMARY));
}
static void ui_render_home(const ui_state_t *state)
{
    browser_header("Home");
    for(int visible=0;visible<2;visible++){
        int row=state->home_scroll+visible;if(row>=5)break;
        int y=POSTER_Y+visible*SHELF_HEIGHT;
        draw_text(10,y-18,0.44f,rgba(COLOR_TEXT_PRIMARY),s_shelf_names[row]);
        const jfin_item_list_t *list=&state->home_rows[row];
        if(!list->count){
            const char *message=!state->home_loaded[row]?"Loading...":state->home_ok[row]?"No items yet":"Unable to load. L retries.";
            draw_text(10,y+20,0.38f,rgba(COLOR_TEXT_SECONDARY),message);
            if(!state->home_loaded[row])draw_loading_ring(285,y+36);
        }
        for(int col=0;col<POSTER_COLUMNS;col++){
            int index=state->home_offset[row]+col;if(index>=list->count)break;
            draw_shelf_poster(state,&list->items[index],col,y,row==state->home_row && index==state->home_selected[row]);
        }
        if(list->count>POSTER_COLUMNS){char count[32];snprintf(count,sizeof(count),"%d/%d",state->home_selected[row]+1,list->count);
            draw_text(275,y-17,0.28f,rgba(COLOR_TEXT_SECONDARY),count);}
    }
}
void ui_render_browse(const ui_state_t *state)
{
    const char *title=state->current_view==VIEW_LIBRARIES?"Library":state->parent_depth?state->parent_stack_names[state->parent_depth-1]:"Search results";
    browser_header(title);
    for(int row=0;row<2;row++){
        int y=POSTER_Y+row*SHELF_HEIGHT;
        draw_text(10,y-18,0.40f,rgba(COLOR_TEXT_PRIMARY),row==0?"Browse":"More titles");
        for(int col=0;col<POSTER_COLUMNS;col++){
            int i=state->scroll_offset+row*POSTER_COLUMNS+col;if(i>=state->items.count)break;
            draw_shelf_poster(state,&state->items.items[i],col,y,i==state->selected_index);
        }
    }
    if(!state->items.count)draw_text(10,80,0.4f,rgba(COLOR_TEXT_SECONDARY),"No items found");
    char count[48];snprintf(count,sizeof(count),"%d/%d",state->items.count?state->selected_index+1:0,state->items.count);
    draw_text(270,32,0.28f,rgba(COLOR_TEXT_SECONDARY),count);
}
void ui_render_libraries(const ui_state_t *state){ui_render_browse(state);}
static void draw_navigation(const ui_state_t *state)
{
    C2D_SceneBegin(s_bottom);
    C2D_DrawRectSolid(0,NAV_Y,0.8f,320,32,rgba(COLOR_BG_CARD));
    int active=state->current_view==VIEW_HOME?0:state->current_view==VIEW_DOWNLOADS?2:state->current_view==VIEW_SETTINGS?3:1;
    const char *labels[]={"Home","Library","Downloads","Settings"};
    for(int i=0;i<4;i++){
        float x=i*80;u32 color=rgba(i==active?COLOR_PRIMARY:COLOR_TEXT_SECONDARY);
        if(i)C2D_DrawRectSolid(x,NAV_Y,0.9f,1,32,rgba(COLOR_SEPARATOR));
        if(i==active)C2D_DrawRectSolid(x,NAV_Y,0.9f,80,2,rgba(COLOR_PRIMARY));
        /* Small geometric symbols remain legible at native handheld resolution. */
        if(i==0){C2D_DrawRectSolid(x+35,216,0.9f,10,7,color);C2D_DrawRectSolid(x+37,213,0.9f,6,3,color);}
        else if(i==1){C2D_DrawRectSolid(x+34,213,0.9f,4,11,color);C2D_DrawRectSolid(x+40,213,0.9f,5,11,color);}
        else if(i==2){C2D_DrawRectSolid(x+38,212,0.9f,4,8,color);C2D_DrawRectSolid(x+35,217,0.9f,10,2,color);C2D_DrawRectSolid(x+37,219,0.9f,6,2,color);C2D_DrawRectSolid(x+34,224,0.9f,12,2,color);}
        else {C2D_DrawRectSolid(x+35,215,0.9f,10,6,color);C2D_DrawRectSolid(x+38,212,0.9f,4,12,color);}
        C2D_Text text;C2D_TextParse(&text,s_text_buf,labels[i]);C2D_TextOptimize(&text);float width=0;
        C2D_TextGetDimensions(&text,0.29f,0.29f,&width,NULL);
        C2D_DrawText(&text,C2D_WithColor,x+(80-width)/2,229,0.95f,0.29f,0.29f,color);
    }
}

void ui_render_now_playing(const ui_state_t *state, const player_status_t *player)
{
    video_status_t vstatus = video_player_get_status();
    bool is_video = (vstatus.state != VIDEO_STOPPED);

    /* Top screen */
    C2D_TargetClear(s_top, rgba(COLOR_BG_DARK));
    C2D_SceneBegin(s_top);

    if (!state->has_now_playing) {
        draw_text(120, 110, 0.7f, rgba(COLOR_TEXT_SECONDARY), "Nothing playing");
        return;
    }

    if (is_video && vstatus.state == VIDEO_ERROR) {
        /* Show error prominently on top screen */
        draw_text(50, 60, 0.7f, rgba(0xFF4444FF), "Playback Error");
        draw_text(30, 100, 0.5f, rgba(COLOR_TEXT_PRIMARY),
                  vstatus.error_msg[0] ? vstatus.error_msg : "Cannot play this content");
        draw_text(30, 140, 0.5f, rgba(COLOR_TEXT_SECONDARY),
                  state->now_playing.name);
        draw_text(60, 190, 0.45f, rgba(COLOR_TEXT_SECONDARY),
                  "Press any button to go back");
    } else if (is_video && vstatus.state == VIDEO_LOADING) {
        /* Show buffering indicator while video is loading */
        draw_loading_ring(200, 75);
        draw_text(130, 100, 0.7f, rgba(COLOR_PRIMARY), "Buffering...");
        draw_text(80, 135, 0.45f, rgba(COLOR_TEXT_SECONDARY),
                  state->now_playing.name);
    } else if (is_video) {
        /* Render video frame on top screen */
        video_player_render_frame();

        /* Right eye for stereoscopic 3D (only when 3D slider is up) */
        if (vstatus.is_3d && osGet3DSliderState() > 0.0f) {
            C2D_TargetClear(s_top_right, rgba(0x000000FF));
            C2D_SceneBegin(s_top_right);
            video_player_render_frame_right();
        }
    } else if (player->state == PLAYER_LOADING) {
        /* Show buffering indicator while audio is loading */
        draw_loading_ring(200, 75);
        draw_text(130, 100, 0.7f, rgba(COLOR_PRIMARY), "Buffering...");
        draw_text(80, 135, 0.45f, rgba(COLOR_TEXT_SECONDARY),
                  state->now_playing.name);
    } else {
        /* Audio-only: show track info */
        const jfin_item_t *item = &state->now_playing;

        /* Album art or placeholder */
        draw_rect(125, 20, 150, 150, rgba(COLOR_BG_CARD));
        if (album_art_is_loaded())
            album_art_draw(125, 20, 150);
        else
            draw_text(165, 85, 0.6f, rgba(COLOR_TEXT_SECONDARY), "ART");

        draw_text(50, 180, 0.6f, rgba(COLOR_TEXT_PRIMARY), item->name);

        if (item->artist[0])
            draw_text(50, 200, 0.45f, rgba(COLOR_ACCENT), item->artist);

        if (item->album[0])
            draw_text(50, 215, 0.4f, rgba(COLOR_TEXT_SECONDARY), item->album);
    }

    /* Bottom screen: black when hidden (night mode), controls otherwise */
    C2D_TargetClear(s_bottom, rgba(state->bottom_hidden ? 0x000000FF : COLOR_BG_DARK));
    C2D_SceneBegin(s_bottom);

    if (state->bottom_hidden)
        return; /* black bottom screen — just the clear above */

    /* Use video position/state if video is playing, otherwise audio */
    int64_t pos_ticks, dur_ticks;
    int buf_pct;
    const char *state_str = "STOPPED";

    if (is_video) {
        pos_ticks = vstatus.position_ticks;
        dur_ticks = vstatus.duration_ticks;
        buf_pct = vstatus.buffer_percent;
        switch (vstatus.state) {
        case VIDEO_LOADING:  state_str = "BUFFERING..."; break;
        case VIDEO_PLAYING:  state_str = "PLAYING"; break;
        case VIDEO_PAUSED:   state_str = "PAUSED"; break;
        case VIDEO_ERROR:    state_str = vstatus.error_msg; break;
        default: break;
        }
    } else {
        pos_ticks = player->position_ticks;
        dur_ticks = player->duration_ticks;
        buf_pct = player->buffer_percent;
        switch (player->state) {
        case PLAYER_LOADING:  state_str = "BUFFERING..."; break;
        case PLAYER_PLAYING:  state_str = "PLAYING"; break;
        case PLAYER_PAUSED:   state_str = "PAUSED"; break;
        case PLAYER_ERROR:    state_str = player->error_msg; break;
        default: break;
        }
    }

    char context[160];
    const jfin_item_t *media = &state->now_playing;
    snprintf(context, sizeof(context), "%.56s", media->name);
    draw_wrapped(10, 4, 0.45f, 300, 1, 0, context);
    if (media->type == JFIN_ITEM_EPISODE)
        snprintf(context, sizeof(context), "%.44s  S%d E%d", media->series_name,
                 media->season_number, media->index_number);
    else if (media->artist[0])
        snprintf(context, sizeof(context), "%.44s - %.44s", media->artist, media->album);
    else
        snprintf(context, sizeof(context), "%s", media->year > 0 ? "Movie" : "Media");
    draw_wrapped(10, 19, 0.36f, 300, 1, 0, context);
    if (state->seeking) pos_ticks = state->seek_preview_ticks;
    if (dur_ticks <= 0) dur_ticks = media->runtime_ticks;
    /* Progress bar */
    float progress = 0.0f;
    if (dur_ticks > 0)
        progress = (float)pos_ticks / (float)dur_ticks;
    if (progress > 1.0f) progress = 1.0f;
    if (progress < 0.0f) progress = 0.0f;

    draw_rect(TIMELINE_X, 40, TIMELINE_WIDTH, 10, rgba(COLOR_BG_CARD));
    draw_rect(TIMELINE_X, 40, TIMELINE_WIDTH * progress, 10, rgba(COLOR_PRIMARY));

    /* Time labels */
    char pos_str[16], dur_str[16];
    format_ticks(pos_ticks, pos_str, sizeof(pos_str));
    format_ticks(dur_ticks, dur_str, sizeof(dur_str));
    draw_text(20, 50, 0.4f, rgba(COLOR_TEXT_SECONDARY), pos_str);
    draw_text(270, 50, 0.4f, rgba(COLOR_TEXT_SECONDARY), dur_str);

    draw_text(75, 80, 0.45f, rgba(COLOR_PRIMARY),
              state->seeking ? "Release to seek" : state_str);
    draw_text(235,5,0.30f,rgba(COLOR_PRIMARY),state->playback_from_sd?"SD card":"Streaming");
    draw_text(40, 150, 0.4f, rgba(COLOR_ACCENT),
              dur_ticks > 0 ? "Touch / drag the bar to seek" : "Seeking unavailable: unknown duration");

    /* Buffer indicator */
    char buf_str[64];
    snprintf(buf_str, sizeof(buf_str), "Buffer: %d%%", buf_pct);
    draw_text(115, 100, 0.4f, rgba(COLOR_TEXT_SECONDARY), buf_str);

    /* Diagnostics for video playback */
    if (is_video) {
        char diag[80];
        snprintf(diag, sizeof(diag), "%sDec: %.0f fps  Disp: %.0f fps  %dx%d",
                 vstatus.is_3d ? "3D  " : "",
                 vstatus.decode_fps, vstatus.display_fps,
                 vstatus.video_width, vstatus.video_height);
        draw_text(30, 125, 0.38f, rgba(COLOR_TEXT_SECONDARY), diag);
    }

    /* Controls hint */
    draw_text(20, 180, 0.45f, rgba(COLOR_TEXT_PRIMARY),
              "A:Pause X:Stop B:Back L/R:Seek");
}

static int draw_wrapped(float x, float y, float size, float width,
                          int max_lines, int skip, const char *text)
{ return draw_wrapped_color(x,y,size,width,max_lines,skip,text,rgba(COLOR_TEXT_SECONDARY)); }
static int draw_wrapped_color(float x, float y, float size, float width,
                          int max_lines, int skip, const char *text, u32 color)
{ return draw_wrapped_depth(x,y,size,width,max_lines,skip,text,color,0.5f); }
static int draw_wrapped_depth(float x, float y, float size, float width,
                          int max_lines, int skip, const char *text, u32 color, float depth)
{
    int line_no = 0;
    while (*text && (max_lines == 0 || line_no < skip + max_lines)) {
        char line[256] = {0};
        size_t n = 0, last_space = 0;
        const char *begin = text;
        while (*text && *text != '\n' && n < sizeof(line) - 5) {
            unsigned char ch = (unsigned char)*text;
            size_t bytes = ch < 0x80 ? 1 : (ch < 0xE0 ? 2 : (ch < 0xF0 ? 3 : 4));
            size_t available = 0;
            while (available < bytes && text[available]) available++;
            if (available < bytes) { text += available; break; }
            memcpy(line + n, text, bytes);
            n += bytes;
            line[n] = '\0';
            C2D_Text measured;
            C2D_TextBufClear(s_measure_buf);
            C2D_TextParse(&measured, s_measure_buf, line);
            float w = 0;
            C2D_TextGetDimensions(&measured, size, size, &w, NULL);
            if (w > width && n > bytes) {
                n -= bytes;
                if (last_space) n = last_space;
                line[n] = '\0';
                text = begin + n;
                break;
            }
            if (ch == ' ') last_space = n - bytes;
            text += bytes;
        }
        if (line_no >= skip && line_no < skip + max_lines)
            draw_text_depth(x, y + (line_no - skip) * 17, size, color, line, depth);
        line_no++;
        while (*text == ' ' || *text == '\n' || *text == '\r') text++;
    }
    return line_no;
}

static void ui_render_details(const ui_state_t *state)
{
    render_selected_card(state);
    browser_header("Information");
    bool playable = item_is_playable(&state->details.item);
    draw_rect(10, 43, 145, 37, rgba(COLOR_BG_CARD));
    draw_text(20, 52, 0.43f, rgba(COLOR_PRIMARY), playable ? "A Play / Resume" : "A Open");
    if (playable) {
        draw_rect(166, 43, 144, 37, rgba(COLOR_BG_CARD));
        draw_text(172, 53, 0.36f, rgba(COLOR_PRIMARY), "X From beginning");
    }
    if(playable){draw_rect(10,90,300,35,rgba(COLOR_BG_CARD));draw_text(20,98,0.43f,rgba(COLOR_PRIMARY),"Y Download to SD card");}
    draw_wrapped(10, 132, 0.36f, 295, 2, 0, state->details.genres);
    draw_text(10, 170, 0.36f, rgba(COLOR_TEXT_SECONDARY), "Up / Down: scroll description");
    draw_text(10, 197, 0.36f, rgba(COLOR_TEXT_SECONDARY), "B Back   ZR Current playback");
}

void ui_render_settings(const ui_state_t *state, const jfin_session_t *session)
{
    C2D_TargetClear(s_bottom, rgba(COLOR_BG_DARK));
    C2D_SceneBegin(s_bottom);

    draw_text(10, 5, 0.55f, rgba(COLOR_PRIMARY), "Settings");

    for (int i = 0; i < UI_MAX_VISIBLE_ITEMS && i + state->settings_scroll < SET_COUNT; i++) {
        int idx = state->settings_scroll + i;
        float y = 30 + i * UI_LIST_ITEM_HEIGHT;

        if (settings_is_separator(idx)) {
            /* Separator: thin line + label */
            float line_y = y + UI_LIST_ITEM_HEIGHT / 2;
            draw_rect(10, line_y, 300, 1, rgba(COLOR_SEPARATOR));
            const char *sep_label = (idx == SET_SEPARATOR_ACCOUNT) ? "Account" : "About";
            draw_text(15, line_y + 4, 0.4f, rgba(COLOR_TEXT_SECONDARY), sep_label);
            continue;
        }

        bool selected = (idx == state->settings_index);
        draw_list_item_bg(y, 310, UI_LIST_ITEM_HEIGHT - 4, selected);

        /* Label + value per item */
        const char *label = "";
        char value[128] = {0};
        u32 value_color = rgba(COLOR_VALUE);

        switch (idx) {
        case SET_AUDIO_BITRATE:
            label = "Audio Bitrate";
            snprintf(value, sizeof(value), "%d kbps", g_config.audio_bitrate);
            break;
        case SET_VIDEO_BITRATE:
            label = "Video Bitrate";
            snprintf(value, sizeof(value), "%d kbps", g_config.video_bitrate);
            break;
        case SET_AUTO_ADVANCE:
            label = "Auto-advance";
            snprintf(value, sizeof(value), "%s", g_config.auto_advance ? "On" : "Off");
            break;
        case SET_CACHE_CLEAR:
            label = "Offline Cache";
            snprintf(value, sizeof(value), "%llu MB  A: clear",
                     (unsigned long long)(s_cache_bytes_ui / (1024 * 1024)));
            break;
        case SET_SERVER:
            label = "Server";
            snprintf(value, sizeof(value), "%.30s%s",
                     session->server_url,
                     strlen(session->server_url) > 30 ? "..." : "");
            value_color = rgba(COLOR_TEXT_SECONDARY);
            break;
        case SET_USERNAME:
            label = "User";
            snprintf(value, sizeof(value), "%s", g_config.username);
            value_color = rgba(COLOR_TEXT_SECONDARY);
            break;
        case SET_LOGOUT:
            label = "Logout";
            value_color = rgba(COLOR_DANGER);
            break;
        case SET_UPDATE:
            label="Update";
            if(s_update_installed)snprintf(value,sizeof(value),"Restart app");
            else if(s_update.available)snprintf(value,sizeof(value),"A: Install %.20s",s_update.version);
            else snprintf(value,sizeof(value),"A: Check for updates");
            break;
        case SET_VERSION:
            label = "Version";
            snprintf(value, sizeof(value), "v" JFIN_VERSION);
            value_color = rgba(COLOR_TEXT_SECONDARY);
            break;
        case SET_DEVICE_ID:
            label = "Device";
            snprintf(value, sizeof(value), "%.18s%s",
                     g_config.device_id,
                     strlen(g_config.device_id) > 18 ? "..." : "");
            value_color = rgba(COLOR_TEXT_SECONDARY);
            break;
        }

        draw_text(15, y + 10, 0.5f,
                  idx == SET_LOGOUT ? rgba(COLOR_DANGER) : rgba(COLOR_TEXT_PRIMARY),
                  label);
        if (value[0])
            draw_text(200, y + 10, 0.45f, value_color, value);
    }

    draw_text(10, 193, 0.30f, rgba(COLOR_TEXT_SECONDARY),
              "A:Toggle L/R:Change B:Back");
}

/* ── Main render dispatch ──────────────────────────────────────────── */

void ui_render(const ui_state_t *state, const jfin_session_t *session,
               const player_status_t *player)
{
    /* Enable stereoscopic 3D only while a 3D frame is actually being
     * drawn (PLAYING/PAUSED) AND the slider is up. The right-eye render
     * is slider-gated, so without the slider term here a slider at 0
     * would present a stale right framebuffer; with it, slider 0 falls
     * back to clean 2D (left eye full-res) per the design doc. */
    video_status_t vs_3d = video_player_get_status();
    gfxSet3D(state->current_view == VIEW_NOW_PLAYING && vs_3d.is_3d &&
             (vs_3d.state == VIDEO_PLAYING || vs_3d.state == VIDEO_PAUSED) &&
             osGet3DSliderState() > 0.0f);

    C2D_TextBufClear(s_text_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    album_art_update();

    s_render_session = session;
    /* Top screen: selected poster or current playback */
    if (state->current_view == VIEW_NOW_PLAYING) {
        ui_render_now_playing(state, player);
    } else if (state->current_view == VIEW_HOME || state->current_view == VIEW_BROWSE ||
               state->current_view == VIEW_LIBRARIES) {
        render_selected_card(state);
    } else {
        C2D_TargetClear(s_top, rgba(COLOR_BG_DARK));
        C2D_SceneBegin(s_top);

        draw_text(100, 80, 1.0f, rgba(COLOR_PRIMARY), "Jellyfin Touch");
        draw_text(130, 120, 0.5f, rgba(COLOR_TEXT_SECONDARY), "v" JFIN_VERSION);

        /* Show mini now-playing bar if something is playing */
        if (state->has_now_playing && player->state == PLAYER_PLAYING) {
            draw_rect(0, 210, 400, 30, rgba(COLOR_BG_CARD));
            draw_text(10, 215, 0.45f, rgba(COLOR_TEXT_PRIMARY),
                      state->now_playing.name);
            draw_text(340, 215, 0.4f, rgba(COLOR_ACCENT), "Playing");
        }
    }

    /* Bottom screen: view-specific */
    switch (state->current_view) {
    case VIEW_HOME:
        ui_render_home(state);
        break;
    case VIEW_LOGIN:
        ui_render_login(state);
        break;
    case VIEW_LIBRARIES:
        ui_render_libraries(state);
        break;
    case VIEW_BROWSE:
        ui_render_browse(state);
        break;
    case VIEW_NOW_PLAYING:
        /* Already rendered above (both screens) */
        break;
    case VIEW_DETAILS:
        ui_render_details(state);
        break;
    case VIEW_DOWNLOADS:
        browser_header("Downloads");
        if(!state->download_count)draw_wrapped(18,55,0.48f,282,4,0,"No saved downloads. Open a movie, episode or song and choose Download to SD card.");
        for(int row=0;row<3 && row+state->download_scroll<state->download_count;row++){
            int i=row+state->download_scroll;const download_t *d=&state->downloads[i];float y=35+row*41;
            draw_list_item_bg(y,310,38,i==state->download_selected);
            draw_wrapped_color(14,y+2,0.40f,292,1,0,d->details.item.name,rgba(COLOR_TEXT_PRIMARY));
            char size[80];snprintf(size,sizeof(size),"%s  -  %.1f MB on SD card",d->details.item.type==JFIN_ITEM_AUDIO?"Music":"Video",(double)d->bytes/1048576.0);
            draw_text(14,y+22,0.31f,rgba(COLOR_TEXT_SECONDARY),size);
        }
        if(state->download_count){
            draw_rect(10,160,145,38,rgba(COLOR_BG_CARD));draw_text(17,170,0.39f,rgba(COLOR_PRIMARY),"A Play offline");
            draw_rect(165,160,145,38,rgba(COLOR_BG_CARD));draw_text(176,170,0.39f,rgba(COLOR_DANGER),"X Delete");
        }
        break;
    case VIEW_SETTINGS:
        ui_render_settings(state, session);
        break;
    }

    if (state->current_view != VIEW_LOGIN && state->current_view != VIEW_NOW_PLAYING)
        draw_navigation(state);

    if (state->message[0] && !state->bottom_hidden) {
        C2D_SceneBegin(s_bottom);
        C2D_DrawRectSolid(0,168,0.8f,320,40,rgba(COLOR_BG_CARD));
        draw_wrapped_depth(5,169,0.32f,310,2,0,state->message,rgba(COLOR_TEXT_PRIMARY),0.95f);
    }
    C3D_FrameEnd(0);
}
