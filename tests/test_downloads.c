#include <3ds.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "util/downloads.h"
#include "util/cache.h"
#include "util/net.h"
void log_write(const char *fmt,...) {(void)fmt;}
static int calls;
static bool continue_transfer(uint64_t bytes,uint64_t total,void *ctx){(void)bytes;(void)total;(void)ctx;calls++;return true;}
static bool cancel(uint64_t bytes,uint64_t total,void *ctx){(void)bytes;(void)total;(void)ctx;return false;}
static bool delayed_cancel(uint64_t bytes,uint64_t total,void *ctx){(void)bytes;(void)total;return osGetTime()-*(u64 *)ctx<150;}
static void equal_files(const char *a,const char *b){
    FILE *x=fopen(a,"rb"),*y=fopen(b,"rb");assert(x&&y);
    int c,d;do{c=fgetc(x);d=fgetc(y);assert(c==d);}while(c!=EOF);assert(!ferror(x)&&!ferror(y));fclose(x);fclose(y);
}
int main(int argc,char **argv){
    assert(argc==4);assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);mkdir("sdmc:",0755);cache_init();cache_clear();
    jfin_session_t s={0};snprintf(s.server_url,sizeof(s.server_url),"%s",argv[1]);strcpy(s.user_id,"user");strcpy(s.access_token,"must-not-be-saved");
    jfin_item_details_t movie={0};movie.item.type=JFIN_ITEM_MOVIE;movie.item.runtime_ticks=200000000;
    strcpy(movie.item.id,"movie");strcpy(movie.item.name,"Offline test movie");strcpy(movie.overview,"A 20-second test with a visible clock and tone.");
    char url[2048];snprintf(url,sizeof(url),"%s/download-movie",argv[1]);
    assert(download_transfer(&s,&movie,url,continue_transfer,NULL)&&calls>0);
    download_t saved,list[5];assert(download_find(&s,"movie",&saved));assert(saved.bytes>0);
    assert(!strcmp(saved.details.overview,movie.overview));assert(!download_transfer(&s,&movie,url,continue_transfer,NULL));
    char path[512];assert(cache_path(saved.key,"ts",path,sizeof(path)));equal_files(path,argv[2]);
    /* Copy the actual downloaded file for host decoding and seeking checks. */
    FILE *in=fopen(path,"rb"),*out=fopen("downloaded-movie.ts","wb");assert(in&&out);char buf[8192];size_t n;
    while((n=fread(buf,1,sizeof(buf),in)))assert(fwrite(buf,1,n,out)==n);fclose(in);assert(fclose(out)==0);
    char metadata[512];cache_path(saved.key,"json",metadata,sizeof(metadata));FILE *meta=fopen(metadata,"rb");assert(meta);
    char text[16385];size_t ml=fread(text,1,sizeof(text)-1,meta);text[ml]=0;fclose(meta);assert(!strstr(text,s.access_token));
    cache_init();assert(download_list(&s,list,5)==1);assert(cache_has(saved.key,"ts"));
    jfin_session_t other=s;strcpy(other.user_id,"other");assert(download_list(&other,list,5)==0);
    strcpy(other.server_url,"https://different");assert(download_list(&other,list,5)==0);
    assert(download_delete(&saved));assert(!download_find(&s,"movie",&saved));
    jfin_item_details_t audio=movie;audio.item.type=JFIN_ITEM_AUDIO;strcpy(audio.item.id,"song");
    snprintf(url,sizeof(url),"%s/download-song",argv[1]);assert(download_transfer(&s,&audio,url,continue_transfer,NULL));
    assert(download_find(&s,"song",&saved));cache_path(saved.key,"mp3",path,sizeof(path));equal_files(path,argv[3]);assert(download_delete(&saved));
    for(int mode=0;mode<4;mode++){
        const char *route[]={"empty","short","error","json"};snprintf(url,sizeof(url),"%s/download-%s",argv[1],route[mode]);
        assert(!download_transfer(&s,&movie,url,continue_transfer,NULL));assert(download_list(&s,list,5)==0);
    }
    snprintf(url,sizeof(url),"%s/download-movie",argv[1]);assert(!download_transfer(&s,&movie,url,cancel,NULL));
    snprintf(url,sizeof(url),"%s/shutdown-stall-body",argv[1]);u64 start=osGetTime();
    assert(!download_transfer(&s,&movie,url,delayed_cancel,&start));assert(osGetTime()-start<750);
    char key[24],part[512];download_key(&s,"movie",key);cache_part_path(key,"ts",part,sizeof(part));assert(fopen(part,"rb")==NULL);
    FILE *f=fopen(part,"wb");assert(f);fputs("interrupted",f);fclose(f);cache_init();assert(fopen(part,"rb")==NULL);
    assert(download_list(&s,list,5)==0);curl_global_cleanup();
    puts("PASS: HTTPS movie/music downloads, byte integrity, reboot metadata, account isolation, deletion, incomplete/error/empty rejection and prompt cancellation");
}
