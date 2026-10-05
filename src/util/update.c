#include "util/update.h"
#include "util/net.h"
#include "util/sha256.h"
#include "api/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#ifndef UPDATE_API_ENDPOINT
#define UPDATE_API_ENDPOINT UPDATE_API
#endif
#ifndef UPDATE_TARGET_PATH
#define UPDATE_TARGET_PATH UPDATE_APP_PATH
#endif
#define MAX_APP (32u*1024u*1024u)
typedef struct {
 char *json; size_t count; FILE *file; uint64_t total; jfin_sha256_t hash;
 update_progress_t progress;void *context; unsigned char header[32];size_t header_count;
} request_t;
static const char *string(cJSON *o,const char *key){cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(v)?v->valuestring:"";}
static bool version(const char *text,unsigned out[3]) {
 if(!strncmp(text,"touch-",6))text+=6;if(*text=='v')text++;
 for(int i=0;i<3;i++){
  if(*text<'0'||*text>'9')return false;unsigned value=0;
  do{value=value*10+(unsigned)(*text++-'0');if(value>9999)return false;}while(*text>='0'&&*text<='9');out[i]=value;
  if(i<2){if(*text++!='.')return false;}else if(*text)return false;
 }return true;
}
static bool newer(const char *a,const char *b){unsigned x[3],y[3];if(!version(a,x)||!version(b,y))return false;for(int i=0;i<3;i++)if(x[i]!=y[i])return x[i]>y[i];return false;}
static bool digest_valid(const char *s){if(strlen(s)!=64)return false;for(int i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return true;}
bool update_parse_release(const char *json,const char *current,update_info_t *out){
 unsigned current_parts[3];memset(out,0,sizeof(*out));if(!version(current,current_parts))return false;
 cJSON *root=cJSON_Parse(json);if(!root)return false;bool ok=false;
 const char *tag=string(root,"tag_name");unsigned parts[3];
 if(strlen(tag)>=sizeof(out->version)||tag[0]!='v'||!version(tag,parts)||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"draft"))||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"prerelease")))goto done;
 cJSON *assets=cJSON_GetObjectItemCaseSensitive(root,"assets"),*asset;
 if(!cJSON_IsArray(assets))goto done;
 cJSON_ArrayForEach(asset,assets){
  if(strcmp(string(asset,"name"),"jellyfin-3ds.3dsx"))continue;
  const char *digest=string(asset,"digest"),*url=string(asset,"browser_download_url");
  cJSON *size=cJSON_GetObjectItemCaseSensitive(asset,"size");
  char expected[512];int n=snprintf(expected,sizeof(expected),"https://github.com/" UPDATE_REPO "/releases/download/%s/jellyfin-3ds.3dsx",tag);
  if(n<0||(size_t)n>=sizeof(expected)||strcmp(url,expected)||strncmp(digest,"sha256:",7)||!digest_valid(digest+7)||!cJSON_IsNumber(size)||!isfinite(size->valuedouble)||size->valuedouble<32||size->valuedouble>MAX_APP||floor(size->valuedouble)!=size->valuedouble)goto done;
  snprintf(out->version,sizeof(out->version),"%s",tag);snprintf(out->url,sizeof(out->url),"%s",url);snprintf(out->sha256,sizeof(out->sha256),"%s",digest+7);out->size=(uint64_t)size->valuedouble;out->available=newer(tag,current);ok=true;break;
 }
done:cJSON_Delete(root);return ok;
}
static size_t write_response(void *data,size_t size,size_t count,void *context){
 request_t *r=context;if(size&&count>SIZE_MAX/size)return 0;size_t n=size*count;
 if(r->file){
  if(r->count>r->total||n>r->total-r->count)return 0;
  for(size_t i=0;i<n&&r->header_count<32;i++)r->header[r->header_count++]=((unsigned char *)data)[i];
  size_t written=fwrite(data,1,n,r->file);jfin_sha256_update(&r->hash,data,written);r->count+=written;return written;
 }
 if(r->count>65536||n>65536-r->count)return 0;
 char *p=realloc(r->json,r->count+n+1);if(!p)return 0;r->json=p;memcpy(p+r->count,data,n);r->count+=n;p[r->count]=0;return n;
}
static bool pump(void *context){request_t *r=context;return !r->progress||r->progress(r->count,r->total,r->context);}
static CURLcode fetch(const char *url,request_t *r,bool asset,long *http){
 CURL *c=curl_easy_init();if(!c)return CURLE_FAILED_INIT;net_configure(c);
 curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_USERAGENT,"Jellyfin-Touch-3DS/" JFIN_VERSION);
 curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,write_response);curl_easy_setopt(c,CURLOPT_WRITEDATA,r);
 curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_TIMEOUT,asset?120L:20L);
 /* This request never carries a Jellyfin token. GitHub assets redirect to its CDN. */
 if(asset){curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,3L);}
#if LIBCURL_VERSION_NUM >= 0x075500
 curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#else
 curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
#endif
 CURLcode result=net_perform_pumped(c,pump,r);curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,http);curl_easy_cleanup(c);return result;
}
bool update_check(update_info_t *out,update_progress_t progress,void *context,char *message,size_t length){
 memset(out,0,sizeof(*out));request_t r={.progress=progress,.context=context};long http=0;
 CURLcode result=fetch(UPDATE_API_ENDPOINT,&r,false,&http);
 bool ok=result==CURLE_OK&&http==200&&r.json&&update_parse_release(r.json,JFIN_VERSION,out);free(r.json);
 if(ok)snprintf(message,length,out->available?"Update %s available. Press Update again to install.":"You are up to date (%s).",out->version);
 else snprintf(message,length,"%s",result==CURLE_ABORTED_BY_CALLBACK?"Update check cancelled.":http==404?"No published release yet.":http==403||http==429?"GitHub limit reached. Try again later.":"Update check failed. Check Wi-Fi, clock and certificates.");
 return ok;
}
static bool header_valid(const request_t *r){
 if(r->header_count<32||memcmp(r->header,"3DSX",4))return false;
 unsigned header=r->header[4]|(unsigned)r->header[5]<<8;
 return header>=32 && header<=r->count && r->header[6]==0 && r->header[7]==0;
}
bool update_install(const update_info_t *info,update_progress_t progress,void *context,char *message,size_t length){
 /* Revalidate the URL and metadata even if the caller supplies its own object. */
 unsigned parts[3];char expected[512];int n=snprintf(expected,sizeof(expected),"https://github.com/" UPDATE_REPO "/releases/download/%s/jellyfin-3ds.3dsx",info->version);
 if(!info->available||info->version[0]!='v'||!version(info->version,parts)||n<0||(size_t)n>=sizeof(expected)||strcmp(expected,info->url)||!digest_valid(info->sha256)||info->size<32||info->size>MAX_APP){snprintf(message,length,"Invalid update information.");return false;}
 const char *part=UPDATE_TARGET_PATH ".part",*backup=UPDATE_TARGET_PATH ".bak";
 remove(part);request_t r={.total=info->size,.progress=progress,.context=context};jfin_sha256_init(&r.hash);
 r.file=fopen(part,"wb");if(!r.file){snprintf(message,length,"Cannot write update. Check SD space.");return false;}
 long http=0;
#ifdef UPDATE_TEST_ASSET_URL
 const char *url=UPDATE_TEST_ASSET_URL;
#else
 const char *url=info->url;
#endif
 CURLcode result=fetch(url,&r,true,&http);bool disk=fflush(r.file)==0;if(fclose(r.file)!=0)disk=false;
 unsigned char digest[32];char hex[65];jfin_sha256_finish(&r.hash,digest);for(int i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
 bool ok=result==CURLE_OK&&http==200&&disk&&r.count==info->size&&header_valid(&r)&&!strcmp(hex,info->sha256);
 if(!ok){remove(part);snprintf(message,length,"%s",result==CURLE_ABORTED_BY_CALLBACK?"Update cancelled. Current app kept.":"Update failed verification. Current app kept.");return false;}
 struct stat st;bool had_app=stat(UPDATE_TARGET_PATH,&st)==0;
 if(had_app){
  if(stat(backup,&st)==0 && remove(backup)!=0){remove(part);snprintf(message,length,"Could not replace update backup.");return false;}
  if(rename(UPDATE_TARGET_PATH,backup)!=0){remove(part);snprintf(message,length,"Could not back up app. Current app kept.");return false;}
 }
 if(rename(part,UPDATE_TARGET_PATH)!=0){
  bool restored=!had_app||rename(backup,UPDATE_TARGET_PATH)==0;remove(part);
  snprintf(message,length,"%s",restored?"Could not install. Current app restored.":"Install failed. Restore the .3dsx.bak file on SD.");return false;
 }
 snprintf(message,length,"Update installed. Exit and reopen the app.");return true;
}
