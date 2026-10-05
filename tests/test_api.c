#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "api/jellyfin.h"
#include "util/config.h"
jfin_config_t g_config={.audio_bitrate=128,.video_bitrate=472};
void log_write(const char *fmt,...) { (void)fmt; }
static int wait_calls;
static void waiting(void *p) { assert(p==&wait_calls);wait_calls++; }
int main(int argc,char **argv) {
    assert(argc==3); assert(jfin_init());
    jfin_set_wait_callback(waiting,&wait_calls);
    jfin_session_t session;
    bool success=jfin_login(&session,argv[1],"user","password","device");
    if (!strcmp(argv[2],"reject")) {
        assert(!success); assert(jfin_last_error()[0]);
        puts("PASS: rejected untrusted certificate or mismatched hostname / redirect");
    } else {
        if (!success) fprintf(stderr,"%s\n",jfin_last_error());
        assert(success);assert(wait_calls>0);
        assert(!strcmp(session.access_token,"test-token"));
        jfin_item_details_t details;
        assert(jfin_get_item_details(&session,"episode",&details));
        assert(details.item.type==JFIN_ITEM_EPISODE);
        assert(details.item.season_number==2 && details.item.index_number==3);
        assert(!strcmp(details.item.series_name,"Test series"));
        assert(!strcmp(details.genres,"Drama, Sci-Fi"));
        assert(!strcmp(details.overview,"An episode description."));
        assert(details.resume_ticks==500000000LL && details.item.resume_ticks==500000000LL);
        assert(details.item.has_series_image && !strcmp(details.item.series_id,"series-id"));
        char image_url[JFIN_URL_BUF];
        jfin_get_image_url_for_item(&session,&details.item,128,192,image_url,sizeof(image_url));
        assert(strstr(image_url,"/Items/series-id/Images/Primary"));
        int stalled=wait_calls;
        assert(jfin_get_item_details(&session,"slow",&details));
        assert(wait_calls-stalled>=3); /* frames are pumped throughout a stalled request */
        int before=wait_calls;
        assert(jfin_get_item_details_background(&session,"episode",&details));
        assert(wait_calls==before);
        jfin_item_list_t shelf;
        for(int row=0;row<5;row++) { assert(jfin_get_home_shelf(&session,row,&shelf)); assert(shelf.count==1 && shelf.items[0].has_primary_image); }
        assert(!jfin_get_home_shelf(&session,5,&shelf));
        assert(jfin_get_item_details(&session,"movie",&details));
        assert(details.item.type==JFIN_ITEM_MOVIE && details.item.year==2024);
        assert(strlen(details.overview)==sizeof(details.overview)-1);
        assert(jfin_get_item_details(&session,"track",&details));
        assert(details.item.type==JFIN_ITEM_AUDIO);
        assert(!strcmp(details.item.artist,"Artist"));
        assert(!strcmp(details.item.album,"Album"));
        jfin_stream_t stream;
        assert(jfin_get_video_stream(&session,"episode",900000000LL,false,&stream));
        assert(strstr(stream.url,"StartTimeTicks=900000000"));
        assert(strstr(stream.url,argv[1])==stream.url);
        assert(jfin_get_audio_stream(&session,"track",200000000LL,&stream));
        assert(strstr(stream.url,"startTimeTicks=200000000"));
        assert(!jfin_login(&session,"file:///bad","u","p","d"));
        puts("PASS: HTTPS login, metadata, resume ticks, base path and seek stream URLs");
    }
    jfin_cleanup();
}
