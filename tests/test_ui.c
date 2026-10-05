#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "ui/ui.h"
#include "ui/timeline.h"
#include "video/video_player.h"
#include "util/config.h"
#include "util/cache.h"
#include "ui/album_art.h"
#include "util/net.h"
#include "util/update.h"
jfin_config_t g_config;
#ifdef HOST_APT_CLOSE
bool host_apt_closing;
#endif
static video_status_t video;
static player_status_t audio;
static int starts, pauses, network_calls;
static bool saved_available;
static download_t mock_download;
static char played_url[512];
static int64_t last_seek;
static bool fail_start, metadata_ready, metadata_success;
static int metadata_requests;
void log_write(const char *f,...) { (void)f; }
video_status_t video_player_get_status(void) { return video; }
player_status_t audio_player_get_status(void) { return audio; }
bool video_player_is_supported(void) { return true; }
bool video_player_play(const char *u,int64_t d,int64_t s,vp_3d_mode_t m) {
    snprintf(played_url,sizeof(played_url),"%s",u);(void)m; starts++;last_seek=s;video.duration_ticks=d;
    video.position_ticks=s;video.state=VIDEO_LOADING;return !fail_start;
}
bool audio_player_play(const char *u,int64_t d,int64_t s) { (void)u;(void)d;(void)s;return false; }
void video_player_request_stop(void) {}
void audio_player_request_stop(void) {}
void video_player_stop(void) { video.state=VIDEO_STOPPED; }
void audio_player_stop(void) { audio.state=PLAYER_STOPPED; }
void video_player_pause(void) { pauses++; video.state=VIDEO_PAUSED; }
void audio_player_pause(void) { audio.state=PLAYER_PAUSED; }
void video_player_render_frame(void) {}
void video_player_render_frame_right(void) {}
bool album_art_init(void) { return true; }
void album_art_cleanup(void) {}
void album_art_request_stop(void) {}
void album_art_update(void) {}
void album_art_request(const jfin_session_t *s,const jfin_item_t *i) { (void)s;(void)i; }
bool album_art_draw_item(const jfin_session_t *s,const jfin_item_t *i,float x,float y,float w,float h) { (void)s;(void)i;(void)x;(void)y;(void)w;(void)h;return false; }
void album_art_request_details(const jfin_session_t *s,const char *i) { (void)s;(void)i;metadata_requests++; }
bool album_art_take_details(const char *i,jfin_item_details_t *o,bool *b) { (void)i;if(!metadata_ready)return false;metadata_ready=false;*b=metadata_success;if(*b)strcpy(o->overview,"Loaded description");return true; }
void album_art_request_home(const jfin_session_t *s) { (void)s; }
bool album_art_take_home(int r,jfin_item_list_t *o,bool *b) { (void)r;(void)o;(void)b;return false; }
bool album_art_load(const jfin_session_t *s,const jfin_item_t *i) { (void)s;(void)i;return false; }
bool album_art_is_loaded(void) { return false; }
void album_art_draw(float x,float y,float z) { (void)x;(void)y;(void)z; }
bool cache_has(const char *i,const char *e) { (void)i;(void)e;return false; }
bool cache_path(const char *i,const char *e,char *o,size_t n) {
    if(saved_available){snprintf(o,n,"sdmc:/test/%s.%s",i,e);return true;}return false;
}
bool cache_part_path(const char *i,const char *e,char *o,size_t n) { return cache_path(i,e,o,n); }
bool cache_remove(const char *i,const char *e) { (void)i;(void)e;return false; }
bool cache_is_full(void) { return true; }
uint64_t cache_total_bytes(void) { return 0; }
int cache_clear(void) { return 0; }
void cache_index_add(const char *i,const char *e) { (void)i;(void)e; }
bool config_save(const jfin_config_t *c) { (void)c;return true; }
bool config_save_session(jfin_config_t *c,const char *u,const char *t,const char *i,const char *n) { (void)c;(void)u;(void)t;(void)i;(void)n;return true; }
bool jfin_get_video_stream(const jfin_session_t *s,const char *i,int64_t t,bool d,jfin_stream_t *o) { (void)s;(void)i;(void)t;(void)d;network_calls++;strcpy(o->url,"https://example.com");return !fail_start; }
bool jfin_get_audio_stream(const jfin_session_t *s,const char *i,int64_t t,jfin_stream_t *o) { (void)s;(void)i;(void)t;(void)o;network_calls++;return false; }
bool jfin_report_start(const jfin_session_t *s,const char *i) { (void)s;(void)i;network_calls++;return true; }
bool jfin_report_progress(const jfin_session_t *s,const char *i,int64_t t,bool p) { (void)s;(void)i;(void)t;(void)p;network_calls++;return true; }
bool jfin_get_items(const jfin_session_t *s,const char *i,int a,int b,jfin_item_list_t *o) { (void)s;(void)i;(void)a;(void)b;memset(o,0,sizeof(*o));return true; }
bool jfin_get_views(const jfin_session_t *s,jfin_item_list_t *o) { return jfin_get_items(s,"",0,0,o); }
bool jfin_search(const jfin_session_t *s,const char *q,int n,jfin_item_list_t *o) { return jfin_get_items(s,q,0,n,o); }
bool jfin_login(jfin_session_t *s,const char *u,const char *n,const char *p,const char *d) { (void)s;(void)u;(void)n;(void)p;(void)d;return false; }
void jfin_logout(jfin_session_t *s) { (void)s; }
const char *jfin_last_error(void) { return "test error"; }
bool jfin_get_item_details(const jfin_session_t *s,const char *i,jfin_item_details_t *o) { (void)s;(void)i;memset(o,0,sizeof(*o));strcpy(o->item.name,"Details");return true; }
void jfin_cancel_requests(void) {}
void jfin_set_wait_callback(void (*callback)(void *),void *context) { (void)callback;(void)context; }
void net_configure(CURL *c) { (void)c; }
static void input(ui_state_t *s,u32 down,u32 held,int x,int y) {
    jfin_session_t session={0};
    ui_update(s,&session,down,held,(touchPosition){x,y});
}
static void reset(ui_state_t *s) {
    memset(s,0,sizeof(*s));s->current_view=VIEW_NOW_PLAYING;s->previous_view=VIEW_BROWSE;
    s->has_now_playing=true;s->now_playing.type=JFIN_ITEM_EPISODE;
    s->now_playing.runtime_ticks=36000000000LL;
    strcpy(s->now_playing.id,"episode");video.state=VIDEO_PLAYING;audio.state=PLAYER_STOPPED;
    starts=pauses=0;fail_start=false;
}
#ifdef HOST_RENDER_TRACE
static FILE *trace;
void host_trace(int kind,float x,float y,float w,float h,u32 color,const char *text) {
    if(!trace)return;
    fprintf(trace,"%d\t%.2f\t%.2f\t%.2f\t%.2f\t%u\t",kind,x,y,w,h,color);
    if(text)for(const unsigned char *p=(const unsigned char *)text;*p;p++)fprintf(trace,"%02x",*p);
    fputc('\n',trace);
}
#endif
int main(void) {
    assert(ui_init());ui_state_t s; reset(&s);
    input(&s,KEY_TOUCH,KEY_TOUCH,160,40);
    assert(s.seeking && starts==0 && s.seek_preview_ticks==18000000000LL);
    input(&s,0,KEY_TOUCH,230,120); assert(starts==0);
    input(&s,0,0,0,0); assert(starts==1 && !s.seeking && last_seek==27000000000LL);
    reset(&s);input(&s,KEY_TOUCH,KEY_TOUCH,100,100);input(&s,0,0,0,0);assert(starts==0);
    reset(&s);video.state=VIDEO_PAUSED;input(&s,KEY_TOUCH,KEY_TOUCH,160,40);
    input(&s,0,0,0,0);assert(s.seek_pause_pending && pauses==0);
    video.state=VIDEO_PLAYING;input(&s,0,0,0,0);assert(pauses==1 && !s.seek_pause_pending);
    reset(&s);input(&s,KEY_TOUCH,KEY_TOUCH,160,40);input(&s,KEY_B,0,0,0);
    assert(!s.seeking && starts==0 && s.current_view==VIEW_BROWSE);
    reset(&s);s.now_playing.runtime_ticks=5000000;video.position_ticks=0;
    input(&s,KEY_R,0,0,0);assert(starts==1 && last_seek==0);
    reset(&s);input(&s,KEY_TOUCH,KEY_TOUCH,160,40);fail_start=true;input(&s,0,0,0,0);
    assert(!s.has_now_playing && s.auto_stopped && s.message[0]);
    reset(&s);s.current_view=VIEW_BROWSE;s.items.count=1;
    s.items.items[0].type=JFIN_ITEM_MOVIE;strcpy(s.items.items[0].id,"movie");
    input(&s,KEY_A,0,0,0);assert(s.current_view==VIEW_DETAILS);
    player_status_t ps={0};jfin_session_t session={0};ui_render(&s,&session,&ps);
    input(&s,KEY_B,0,0,0);assert(s.current_view==VIEW_BROWSE);
    reset(&s);s.current_view=VIEW_HOME;s.home_requested=true;
    for(int r=0;r<5;r++) { s.home_rows[r].count=10; for(int i=0;i<10;i++) {
        s.home_rows[r].items[i].type=JFIN_ITEM_MOVIE;
        snprintf(s.home_rows[r].items[i].id,JFIN_MAX_ID,"r%d-i%d",r,i);
    }}
    input(&s,KEY_TOUCH,KEY_TOUCH,220,60);input(&s,0,0,0,0);
    assert(s.home_selected[0]==2 && s.current_view==VIEW_HOME);
    input(&s,KEY_TOUCH,KEY_TOUCH,105,60);input(&s,0,0,0,0);
    assert(s.home_selected[0]==2); /* gutter */
    input(&s,KEY_TOUCH,KEY_TOUCH,250,60);input(&s,0,KEY_TOUCH,30,60);input(&s,0,0,0,0);
    assert(s.home_selected[0]==4 && s.home_offset[0]==2);
    for(int i=0;i<3;i++) input(&s,KEY_DDOWN,0,0,0);
    assert(s.home_row==3 && s.home_scroll==3);
    input(&s,KEY_SELECT,0,0,0);assert(s.current_view==VIEW_SETTINGS);
    input(&s,KEY_B,0,0,0);assert(s.current_view==VIEW_HOME && s.home_row==3);
    s.current_view=VIEW_BROWSE;s.items.count=12;s.selected_index=0;
    input(&s,KEY_DDOWN,0,0,0);assert(s.selected_index==3);
    input(&s,KEY_DDOWN,0,0,0);assert(s.selected_index==6 && s.scroll_offset==6);
    input(&s,KEY_TOUCH,KEY_TOUCH,120,60);input(&s,0,0,0,0);assert(s.selected_index==7);
    input(&s,KEY_TOUCH,KEY_TOUCH,200,220);assert(s.current_view==VIEW_DOWNLOADS && !s.touch_held);
    input(&s,0,0,0,0);ui_render(&s,&session,&ps);
    input(&s,KEY_B,0,0,0);assert(s.current_view==VIEW_BROWSE && s.selected_index==7);
    input(&s,KEY_TOUCH,KEY_TOUCH,280,220);assert(s.current_view==VIEW_SETTINGS);
    input(&s,KEY_B,0,0,0);assert(s.current_view==VIEW_BROWSE);
    input(&s,KEY_TOUCH,KEY_TOUCH,40,220);assert(s.current_view==VIEW_HOME);
    input(&s,KEY_TOUCH,KEY_TOUCH,120,220);assert(s.current_view==VIEW_LIBRARIES);
    input(&s,KEY_TOUCH,KEY_TOUCH,200,220);assert(s.current_view==VIEW_DOWNLOADS);
    input(&s,KEY_TOUCH,KEY_TOUCH,280,220);assert(s.current_view==VIEW_SETTINGS);
    input(&s,KEY_B,0,0,0);assert(s.current_view==VIEW_DOWNLOADS);
    s.current_view=VIEW_DETAILS;s.details_loading=true;s.details.item.type=JFIN_ITEM_MOVIE;
    starts=0;input(&s,KEY_TOUCH,KEY_TOUCH,30,60);assert(starts==0 && s.message[0]);
    reset(&s);s.current_view=VIEW_BROWSE;s.items.count=1;
    strcpy(s.items.items[0].id,"retry-movie");s.items.items[0].type=JFIN_ITEM_MOVIE;
    metadata_ready=true;metadata_success=false;input(&s,0,0,0,0);
    assert(s.preview_failed && !s.preview_ready);metadata_requests=0;
    for(int n=0;n<119;n++)input(&s,0,0,0,0);
    assert(metadata_requests==0);input(&s,0,0,0,0);assert(metadata_requests==1);
    metadata_success=metadata_ready=true;input(&s,0,0,0,0);
    assert(s.preview_ready && !s.preview_failed && !strcmp(s.preview.overview,"Loaded description"));
    reset(&s);s.current_view=VIEW_BROWSE;s.has_now_playing=false;network_calls=0;
    memset(&mock_download,0,sizeof(mock_download));mock_download.details.item.type=JFIN_ITEM_MOVIE;
    mock_download.details.item.runtime_ticks=200000000;strcpy(mock_download.details.item.id,"saved-movie");
    strcpy(mock_download.details.item.name,"Offline test movie");strcpy(mock_download.key,"dl-test");saved_available=true;
    input(&s,KEY_TOUCH,KEY_TOUCH,200,220);assert(s.current_view==VIEW_DOWNLOADS && s.download_count==1);
    input(&s,KEY_TOUCH,KEY_TOUCH,30,175);
    assert(s.current_view==VIEW_NOW_PLAYING && s.playback_offline_only && s.playback_from_sd);
    assert(!strncmp(played_url,"sdmc:",5) && network_calls==0);
    input(&s,0,0,0,0);video.state=VIDEO_PLAYING;input(&s,KEY_TOUCH,KEY_TOUCH,160,40);input(&s,0,0,0,0);
    assert(last_seek==100000000 && network_calls==0 && s.playback_offline_only);
    input(&s,KEY_B,0,0,0);assert(s.current_view==VIEW_DOWNLOADS);
    ui_render(&s,&session,&ps);input(&s,KEY_TOUCH,KEY_TOUCH,240,175);
    assert(!saved_available && !s.has_now_playing && s.download_count==0 && network_calls==0);
    /* A failed saved movie never negotiates a streaming fallback. */
    reset(&s);s.current_view=VIEW_DOWNLOADS;s.has_now_playing=false;s.download_count=1;
    s.downloads[0]=mock_download;saved_available=true;fail_start=true;network_calls=0;
    s.has_now_playing=true;s.auto_advance=true;s.play_queue.count=2;s.playing_index=0;
    s.play_queue.items[1].type=JFIN_ITEM_MOVIE;
    input(&s,KEY_A,0,0,0);assert(s.current_view==VIEW_DOWNLOADS && network_calls==0 && s.message[0] && !s.has_now_playing);
    saved_available=false;fail_start=false;
    puts("PASS: Downloads touch controls, SD-only playback and seeking, stop-before-delete, and no streaming fallback on failure");
    reset(&s);s.current_view=VIEW_SETTINGS;s.settings_scroll=8;
    input(&s,KEY_TOUCH,KEY_TOUCH,40,75);assert(s.settings_index==9 && strstr(s.message,"available"));
    input(&s,KEY_A,0,0,0);assert(strstr(s.message,"installed") && !s.has_now_playing);
    input(&s,KEY_A,0,0,0);assert(strstr(s.message,"reopen"));
    puts("PASS: Settings update touch/button check, install and restart message");
#ifdef HOST_RENDER_TRACE
    memset(&s,0,sizeof(s));s.current_view=VIEW_HOME;s.home_requested=true;
    const char *names[]={"THE LAST ORBIT","Northern Lights","Sea of Stars","Hidden City"};
    for(int r=0;r<2;r++){s.home_rows[r].count=4;for(int i=0;i<4;i++){
        jfin_item_t *item=&s.home_rows[r].items[i];snprintf(item->id,sizeof(item->id),"sample-%d-%d",r,i);
        snprintf(item->name,sizeof(item->name),"%s",names[i]);item->type=JFIN_ITEM_MOVIE;item->year=2026;item->runtime_ticks=56400000000LL;
    }}
    s.preview.item=s.home_rows[0].items[0];s.preview_ready=true;s.preview.resume_ticks=19380000000LL;
    strcpy(s.preview.official_rating,"PG-13");s.preview.community_rating=8.4;
    strcpy(s.preview.genres,"Adventure, Science Fiction");
    strcpy(s.preview.overview,"A small crew follows a mysterious signal beyond the last station. Their journey brings them closer to home than they ever expected.");
    session.authenticated=true;
    s.home_rows[0].items[0].resume_ticks=s.preview.resume_ticks;
    trace=fopen("ui-preview.trace","w");assert(trace);ui_render(&s,&session,&ps);fclose(trace);trace=NULL;
#endif
#ifdef HOST_APT_CLOSE
    reset(&s);s.current_view=VIEW_DETAILS;s.details_loading=false;s.details.item.type=JFIN_ITEM_MOVIE;
    host_apt_closing=true;input(&s,KEY_A,0,0,0);assert(starts==0);
    /* Cleanup must not begin a drawing frame after HOME Menu close. */
#endif
    ui_cleanup();puts("PASS: production UI tap/drag/release, paused seeking, cancel, short clips, failure and details navigation (mock hardware)");
}

void album_art_request_cached(const jfin_session_t *s,const jfin_item_t *i){(void)s;(void)i;}
void download_key(const jfin_session_t *s,const char *i,char k[24]){(void)s;(void)i;strcpy(k,"dl-test");}
const char *download_ext(const jfin_item_t *i){return i->type==JFIN_ITEM_AUDIO?"mp3":"ts";}
bool download_save(const jfin_session_t *s,const jfin_item_details_t *d){(void)s;(void)d;return false;}
int download_list(const jfin_session_t *s,download_t *o,int c){(void)s;if(saved_available && c>0){if(o)*o=mock_download;return 1;}return 0;}
bool download_find(const jfin_session_t *s,const char *i,download_t *o){(void)s;
    if(saved_available && !strcmp(i,mock_download.details.item.id)){*o=mock_download;return true;}return false;}
bool download_delete(const download_t *d){(void)d;assert(video.state==VIDEO_STOPPED && audio.state==PLAYER_STOPPED);saved_available=false;return true;}
CURLcode net_perform_pumped(CURL *c,bool (*p)(void *),void *ctx){(void)c;(void)p;(void)ctx;return CURLE_ABORTED_BY_CALLBACK;}

bool download_transfer(const jfin_session_t *s,const jfin_item_details_t *d,const char *u,download_progress_t p,void *ctx){(void)s;(void)d;(void)u;(void)p;(void)ctx;return false;}

bool album_art_load_cached(const jfin_session_t *s,const jfin_item_t *i){(void)s;(void)i;return true;}

bool update_check(update_info_t *o,update_progress_t p,void *ctx,char *m,size_t n){(void)p;(void)ctx;memset(o,0,sizeof(*o));o->available=true;strcpy(o->version,"v0.4.2");snprintf(m,n,"Update available");return true;}
bool update_install(const update_info_t *o,update_progress_t p,void *ctx,char *m,size_t n){(void)o;(void)p;(void)ctx;snprintf(m,n,"Update installed");return true;}
