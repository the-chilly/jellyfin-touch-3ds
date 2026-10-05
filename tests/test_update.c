#include <3ds.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "util/update.h"
#include "util/sha256.h"
#include "util/net.h"
void log_write(const char *fmt,...){(void)fmt;}
static bool cancelled(uint64_t b,uint64_t t,void *ctx){(void)b;(void)t;(void)ctx;return false;}
static void read_file(const char *path,char *out,size_t capacity){FILE *f=fopen(path,"rb");assert(f);size_t n=fread(out,1,capacity-1,f);out[n]=0;fclose(f);}
static void vector(const char *text,size_t n,const char *expected){jfin_sha256_t h;jfin_sha256_init(&h);for(size_t i=0;i<n;i++)jfin_sha256_update(&h,text+i,1);unsigned char digest[32];char hex[65];jfin_sha256_finish(&h,digest);for(int i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);assert(!strcmp(hex,expected));}
int main(int argc,char **argv){
 assert(argc==3);vector("",0,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
 vector("abc",3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
 const char *long_vector="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
 vector(long_vector,strlen(long_vector),"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
 char *million=malloc(1000000);assert(million);memset(million,'a',1000000);vector(million,1000000,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");free(million);
 assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);update_info_t info;char message[192];
 assert(update_check(&info,NULL,NULL,message,sizeof(message)));assert(info.available&&!strcmp(info.version,"v9.0.0"));
 char json[2048];snprintf(json,sizeof(json),"{\"tag_name\":\"v9.0.0\",\"assets\":[{\"name\":\"jellyfin-3ds.3dsx\",\"size\":1024,\"digest\":\"sha256:%s\",\"browser_download_url\":\"https://github.com/" UPDATE_REPO "/releases/download/v9.0.0/jellyfin-3ds.3dsx\"}]}",argv[2]);
 update_info_t parsed;assert(update_parse_release(json,"touch-0.4.1",&parsed)&&parsed.available);
 assert(update_parse_release(json,"v9.0.0",&parsed)&&!parsed.available);
 assert(update_parse_release(json,"touch-10.0.0",&parsed)&&!parsed.available);
 assert(!update_parse_release("{}","touch-0.4.1",&parsed));
 assert(!update_parse_release("{\"tag_name\":\"v999999999999.0.0\",\"assets\":[]}","touch-0.4.1",&parsed));
 char *https=strstr(json,"https://github.com/");assert(https);https[0]='x';assert(!update_parse_release(json,"touch-0.4.1",&parsed));
 FILE *f=fopen("app.3dsx","wb");assert(f);fputs("old app",f);fclose(f);
 char old[64];update_info_t broken=info;broken.sha256[0]=broken.sha256[0]=='0'?'1':'0';
 assert(!update_install(&broken,NULL,NULL,message,sizeof(message)));read_file("app.3dsx",old,sizeof(old));assert(!strcmp(old,"old app"));assert(fopen("app.3dsx.part","rb")==NULL);
 assert(!update_install(&info,cancelled,NULL,message,sizeof(message)));read_file("app.3dsx",old,sizeof(old));assert(!strcmp(old,"old app"));
 broken=info;broken.size++;assert(!update_install(&broken,NULL,NULL,message,sizeof(message)));read_file("app.3dsx",old,sizeof(old));assert(!strcmp(old,"old app"));
 assert(update_install(&info,NULL,NULL,message,sizeof(message)));read_file("app.3dsx.bak",old,sizeof(old));assert(!strcmp(old,"old app"));
 struct stat st;assert(stat("app.3dsx",&st)==0&&(uint64_t)st.st_size==info.size);read_file("app.3dsx",old,sizeof(old));assert(!memcmp(old,"3DSX",4));
 assert(!update_check(&parsed,cancelled,NULL,message,sizeof(message)));assert(!parsed.available);
 curl_global_cleanup();puts("PASS: SHA-256 vectors, release/version/URL validation, trusted HTTPS check, verified install, backup, bad-hash/short-file rejection and cancellation preservation");
}
