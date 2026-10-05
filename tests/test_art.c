#include <3ds.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "ui/album_art.h"
#include "util/cache.h"
#include "util/config.h"
jfin_config_t g_config={.audio_bitrate=128,.video_bitrate=472};
pthread_t host_gpu_thread;
void log_write(const char *fmt,...) { (void)fmt; }
static void nap(void) { svcSleepThread(10000000LL); }
static bool await_art(jfin_session_t *s,jfin_item_t *i) {
    for(int n=0;n<300;n++) { album_art_update(); if(album_art_draw_item(s,i,0,0,64,74))return true;nap(); }
    return false;
}
int main(int argc,char **argv) {
    assert(argc==2);host_gpu_thread=pthread_self();assert(jfin_init());
    mkdir("sdmc:",0755);cache_init();
    char key[24],other[24];cache_art_key(argv[1],"user-id","movie",key,sizeof(key));
    cache_art_key(argv[1],"other-user","movie",other,sizeof(other));assert(strcmp(key,other));
    cache_art_key("https://different","user-id","movie",other,sizeof(other));assert(strcmp(key,other));
    unsigned char data[]={1,2,3};assert(!cache_art_write("../../escape",data,3));
    assert(cache_art_write(other,data,3));unsigned char *read=NULL;size_t len=0;
    assert(cache_art_read(other,&read,&len) && len==3 && !memcmp(data,read,3));free(read);
    jfin_session_t s;assert(jfin_login(&s,argv[1],"user","password","device"));
    jfin_item_t item={.type=JFIN_ITEM_MOVIE,.has_primary_image=true};strcpy(item.id,"movie");
    jfin_item_t oversized=item, corrupt=item;strcpy(oversized.id,"oversized");strcpy(corrupt.id,"corrupt");
    unsigned char png[]={0x89,'P','N','G',13,10,26,10,0,0,0,13,'I','H','D','R',0,0,8,0,0,0,8,0,8,2,0,0,0,0,0,0,0};
    char bad_key[24];cache_art_key(s.server_url,s.user_id,oversized.id,bad_key,sizeof(bad_key));assert(cache_art_write(bad_key,png,sizeof(png)));
    cache_art_key(s.server_url,s.user_id,corrupt.id,bad_key,sizeof(bad_key));assert(cache_art_write(bad_key,data,sizeof(data)));
    assert(album_art_init());album_art_request(&s,&oversized);album_art_request(&s,&corrupt);
    album_art_request(&s,&item);assert(await_art(&s,&item));
    assert(!album_art_draw_item(&s,&oversized,0,0,64,74));assert(!album_art_draw_item(&s,&corrupt,0,0,64,74));
    assert(cache_art_read(key,&read,&len) && len>3);free(read);
    album_art_request_home(&s);jfin_item_list_t *shelf=calloc(1,sizeof(*shelf));bool ok=false;
    for(int row=0;row<5;row++) { bool ready=false;for(int n=0;n<300 && !ready;n++){ready=album_art_take_home(row,shelf,&ok);if(!ready)nap();} assert(ready && ok && shelf->count==1); }
    free(shelf);
    /* A stalled image cannot delay metadata or the five home shelves. */
    jfin_item_t slow_poster=item;strcpy(slow_poster.id,"slow-poster");
    album_art_request(&s,&slow_poster);svcSleepThread(50000000LL);
    album_art_request_home(&s);album_art_request_details(&s,"track");
    jfin_item_details_t fast_details;bool fast_ready=false;
    for(int n=0;n<75 && !fast_ready;n++){fast_ready=album_art_take_details("track",&fast_details,&ok);if(!fast_ready)nap();}
    assert(fast_ready && ok);
    shelf=calloc(1,sizeof(*shelf));
    for(int row=0;row<5;row++){bool loaded=false;for(int n=0;n<20 && !loaded;n++){loaded=album_art_take_home(row,shelf,&ok);if(!loaded)nap();}assert(loaded && ok);}
    free(shelf);assert(!album_art_draw_item(&s,&slow_poster,0,0,64,74));assert(await_art(&s,&slow_poster));
    album_art_request_details(&s,"slow");svcSleepThread(50000000LL);album_art_request_details(&s,"track");
    jfin_item_details_t details;bool ready=false;
    for(int n=0;n<300 && !ready;n++){ready=album_art_take_details("track",&details,&ok);if(!ready)nap();}
    assert(ready && ok && details.item.type==JFIN_ITEM_AUDIO);
    assert(!album_art_take_details("slow",&details,&ok));
    album_art_request_details(&s,"track");ready=false;
    for(int n=0;n<300 && !ready;n++){ready=album_art_take_details("track",&details,&ok);if(!ready)nap();}
    assert(ready && ok); /* consumed metadata can be requested again */
    album_art_cleanup();
    /* Reload the same artwork with unusable networking: it must use SD cache. */
    strcpy(s.access_token,"invalid");assert(album_art_init());album_art_request(&s,&item);assert(await_art(&s,&item));album_art_cleanup();
    for(int n=0;n<128;n++) { char id[32];snprintf(id,sizeof(id),"id-%d",n);cache_art_key(argv[1],"user",id,key,sizeof(key));cache_art_write(key,data,3); }
    cache_art_key(argv[1],"user","over-cap",key,sizeof(key));assert(!cache_art_write(key,data,3));
    assert(cache_clear()==128);assert(!cache_art_read(key,&read,&len));
    /* Shutdown must join a metadata worker whose server sends no response. */
    assert(album_art_init());album_art_request_details(&s,"shutdown-stall");svcSleepThread(150000000LL);
    struct timespec before,after;clock_gettime(CLOCK_MONOTONIC,&before);
    jfin_cancel_requests();album_art_request_stop();album_art_cleanup();clock_gettime(CLOCK_MONOTONIC,&after);
    double elapsed=after.tv_sec-before.tv_sec+(after.tv_nsec-before.tv_nsec)/1e9;assert(elapsed<.75);
    jfin_cleanup();puts("PASS: asynchronous authenticated HTTPS artwork, scoped SD cache/cap, malformed/oversized image rejection, background shelves, stale metadata rejection and main-thread GPU operations");
}
