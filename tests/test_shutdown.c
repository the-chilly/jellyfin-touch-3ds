#include <3ds.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "util/net.h"
static bool cancel;
static CURLcode result;
static const char *target;
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static size_t discard(void *p,size_t s,size_t n,void *c){(void)p;(void)c;return s*n;}
static void transfer(void *p){
    (void)p;CURL *curl=curl_easy_init();assert(curl);net_configure(curl);
    curl_easy_setopt(curl,CURLOPT_URL,target);curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,discard);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,5L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,10L);
    result=net_perform_cancelable(curl,&cancel);curl_easy_cleanup(curl);
}
int main(int argc,char **argv){
    assert(argc==2);assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    char url[2048];
    for(int mode=0;mode<2;mode++){
        snprintf(url,sizeof(url),"%s/shutdown-stall-%s",argv[1],mode?"body":"headers");target=url;
        __atomic_store_n(&cancel,false,__ATOMIC_RELEASE);
        Thread worker=threadCreate(transfer,NULL,128*1024,1,-1,false);assert(worker);
        svcSleepThread(150000000LL);double begin=seconds();__atomic_store_n(&cancel,true,__ATOMIC_RELEASE);
        threadJoin(worker,U64_MAX);threadFree(worker);
        assert(result==CURLE_ABORTED_BY_CALLBACK && seconds()-begin<.75);
    }
    CURL *curl=curl_easy_init();assert(net_perform_cancelable(curl,&cancel)==CURLE_ABORTED_BY_CALLBACK);curl_easy_cleanup(curl);
    curl_global_cleanup();puts("PASS: idle HTTPS requests cancel during header and body stalls without waiting for transfer timeout");
}
