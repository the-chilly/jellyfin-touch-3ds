/* Exercise the production HTTPS-to-file path without console rendering. */
#include <3ds.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "util/net.h"
#include <string.h>
#include "util/downloads.h"
#include "util/cache.h"
void log_write(const char *fmt,...) {va_list ap;va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);putchar('\n');}
static unsigned pumps;
static bool progress(uint64_t bytes,uint64_t total,void *ctx){(void)bytes;(void)total;(void)ctx;pumps++;return true;}
static double clock_seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char **argv){
    assert(argc==3);assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    mkdir("sdmc:",0755);cache_init();cache_clear();
    jfin_session_t s={0};strcpy(s.server_url,"https://benchmark.invalid");strcpy(s.user_id,"test");
    jfin_item_details_t d={0};d.item.type=JFIN_ITEM_MOVIE;strcpy(d.item.id,"benchmark");strcpy(d.item.name,"Transfer benchmark");
    double begin=clock_seconds();assert(download_transfer(&s,&d,argv[1],progress,NULL));double elapsed=clock_seconds()-begin;
    download_t saved;assert(download_find(&s,d.item.id,&saved));
    char path[512];assert(cache_path(saved.key,"ts",path,sizeof(path)));
    FILE *in=fopen(path,"rb"),*expected=fopen(argv[2],"rb");assert(in&&expected);
    int a,b;do{a=fgetc(in);b=fgetc(expected);assert(a==b);}while(a!=EOF);
    assert(!ferror(in)&&!ferror(expected));fclose(in);fclose(expected);
    printf("BENCH: bytes=%llu elapsed=%.3fs speed=%.1f KiB/s pumps=%u\n",(unsigned long long)saved.bytes,elapsed,saved.bytes/1024.0/elapsed,pumps);
    curl_global_cleanup();
}
