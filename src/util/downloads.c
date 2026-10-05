/* Persist titles independently of the online library. No access tokens are saved. */
#include "util/downloads.h"
#include "util/cache.h"
#include "api/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include "util/net.h"
#include "util/log.h"
void download_key(const jfin_session_t *s,const char *id,char key[24]) {
    char art[24];cache_art_key(s->server_url,s->user_id,id,art,sizeof(art));
    snprintf(key,24,"dl-%s",art+4);
}
const char *download_ext(const jfin_item_t *i) { return i->type==JFIN_ITEM_AUDIO?"mp3":"ts"; }
static void field(cJSON *o,const char *key,char *out,size_t size) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    snprintf(out,size,"%s",cJSON_IsString(v)?v->valuestring:"");
}
static double number(cJSON *o,const char *key) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&fabs(v->valuedouble)<1e15?v->valuedouble:0;
}
bool download_save(const jfin_session_t *s,const jfin_item_details_t *d) {
    char key[24],path[512],part[512],media[512];download_key(s,d->item.id,key);
    if(!cache_path(key,"json",path,sizeof(path)) || !cache_part_path(key,"json",part,sizeof(part)) ||
       !cache_path(key,download_ext(&d->item),media,sizeof(media)))return false;
    struct stat st;if(stat(media,&st)!=0 || st.st_size<=0)return false;
    cJSON *o=cJSON_CreateObject();if(!o)return false;
    cJSON_AddNumberToObject(o,"Schema",1);
    cJSON_AddStringToObject(o,"Server",s->server_url);cJSON_AddStringToObject(o,"User",s->user_id);
    cJSON_AddStringToObject(o,"Id",d->item.id);cJSON_AddStringToObject(o,"Name",d->item.name);
    cJSON_AddStringToObject(o,"Overview",d->overview);cJSON_AddStringToObject(o,"Genres",d->genres);
    cJSON_AddStringToObject(o,"Rating",d->official_rating);cJSON_AddStringToObject(o,"Series",d->item.series_name);
    cJSON_AddStringToObject(o,"Artist",d->item.artist);cJSON_AddStringToObject(o,"Album",d->item.album);
    cJSON_AddStringToObject(o,"SeriesId",d->item.series_id);cJSON_AddStringToObject(o,"AlbumId",d->item.album_id);
    cJSON_AddBoolToObject(o,"PrimaryImage",d->item.has_primary_image);cJSON_AddBoolToObject(o,"SeriesImage",d->item.has_series_image);
    cJSON_AddBoolToObject(o,"AlbumImage",d->item.has_album_image);
    cJSON_AddNumberToObject(o,"Type",d->item.type);cJSON_AddNumberToObject(o,"Year",d->item.year);
    cJSON_AddNumberToObject(o,"Season",d->item.season_number);cJSON_AddNumberToObject(o,"Episode",d->item.index_number);
    cJSON_AddNumberToObject(o,"Resume",(double)d->resume_ticks);
    cJSON_AddNumberToObject(o,"Duration",(double)d->item.runtime_ticks);
    cJSON_AddNumberToObject(o,"CommunityRating",d->community_rating);
    cJSON_AddNumberToObject(o,"Format3D",d->item.video_3d_format);
    char *json=cJSON_PrintUnformatted(o);cJSON_Delete(o);if(!json)return false;
    FILE *f=fopen(part,"wb");bool ok=false;
    if(f){size_t len=strlen(json);ok=fwrite(json,1,len,f)==len;if(fflush(f)!=0)ok=false;if(fclose(f)!=0)ok=false;}
    free(json);if(ok)ok=rename(part,path)==0;if(!ok)remove(part);return ok;
}
static bool read_entry(const jfin_session_t *s,const char *key,download_t *out) {
    char path[512],server[JFIN_MAX_URL],user[JFIN_MAX_ID],expected[24];
    if(!cache_path(key,"json",path,sizeof(path)))return false;
    FILE *f=fopen(path,"rb");if(!f)return false;
    char *buf=malloc(16385);if(!buf){fclose(f);return false;}
    size_t len=fread(buf,1,16385,f);bool valid=!ferror(f)&&len>0&&len<=16384;fclose(f);
    cJSON *o=NULL;if(valid){buf[len]=0;o=cJSON_Parse(buf);}free(buf);if(!o)return false;
    memset(out,0,sizeof(*out));field(o,"Server",server,sizeof(server));field(o,"User",user,sizeof(user));
    field(o,"Id",out->details.item.id,sizeof(out->details.item.id));
    download_key(s,out->details.item.id,expected);
    double raw_type=number(o,"Type");int type=raw_type>=0&&raw_type<=JFIN_ITEM_UNKNOWN?(int)raw_type:JFIN_ITEM_UNKNOWN;
    valid=number(o,"Schema")==1 && !strcmp(server,s->server_url) && !strcmp(user,s->user_id) &&
        !strcmp(key,expected) && out->details.item.id[0] && (type==JFIN_ITEM_AUDIO || type==JFIN_ITEM_MOVIE || type==JFIN_ITEM_EPISODE);
    if(valid){
        jfin_item_details_t *d=&out->details;d->item.type=type;
        field(o,"SeriesId",d->item.series_id,sizeof(d->item.series_id));field(o,"AlbumId",d->item.album_id,sizeof(d->item.album_id));
        d->item.has_primary_image=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"PrimaryImage"));
        d->item.has_series_image=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"SeriesImage"));
        d->item.has_album_image=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"AlbumImage"));
        field(o,"Name",d->item.name,sizeof(d->item.name));field(o,"Overview",d->overview,sizeof(d->overview));
        field(o,"Genres",d->genres,sizeof(d->genres));field(o,"Rating",d->official_rating,sizeof(d->official_rating));
        field(o,"Series",d->item.series_name,sizeof(d->item.series_name));field(o,"Artist",d->item.artist,sizeof(d->item.artist));
        field(o,"Album",d->item.album,sizeof(d->item.album));d->item.year=(int)fmax(0,fmin(9999,number(o,"Year")));
        d->item.season_number=(int)fmax(0,fmin(100000,number(o,"Season")));d->item.index_number=(int)fmax(0,fmin(100000,number(o,"Episode")));
        double duration=number(o,"Duration");valid=duration>=0 && duration<1e15;
        d->item.runtime_ticks=valid?(int64_t)duration:0;
        double resume=number(o,"Resume");d->resume_ticks=resume>=0 && resume<duration?(int64_t)resume:0;
        d->item.resume_ticks=d->resume_ticks;d->community_rating=number(o,"CommunityRating");
        double raw_format=number(o,"Format3D");int format=raw_format>=0&&raw_format<=JFIN_3D_HTAB?(int)raw_format:0;d->item.video_3d_format=format>=0&&format<=JFIN_3D_HTAB?format:JFIN_3D_NONE;
        snprintf(out->key,sizeof(out->key),"%s",key);
        struct stat st;cache_path(key,download_ext(&d->item),path,sizeof(path));
        valid=valid&&stat(path,&st)==0&&st.st_size>0;if(valid)out->bytes=(uint64_t)st.st_size;
    }
    cJSON_Delete(o);return valid;
}
int download_list(const jfin_session_t *s,download_t *out,int capacity) {
    DIR *dir=opendir(CACHE_DIR);if(!dir)return 0;int count=0;struct dirent *e;download_t entry;
    while(count<capacity && (e=readdir(dir))) {
        if(strlen(e->d_name)!=24 || strncmp(e->d_name,"dl-",3) || strcmp(e->d_name+19,".json"))continue;
        char key[24];memcpy(key,e->d_name,19);key[19]=0;
        if(read_entry(s,key,&entry)){if(out)out[count]=entry;count++;}
    }
    closedir(dir);return count;
}
bool download_find(const jfin_session_t *s,const char *id,download_t *out) {
    char key[24];download_key(s,id,key);return read_entry(s,key,out);
}
bool download_delete(const download_t *d) {
    if(!cache_remove(d->key,download_ext(&d->details.item)))return false;
    char path[512];if(cache_path(d->key,"json",path,sizeof(path)))remove(path);return true;
}

typedef struct { FILE *file; uint64_t bytes,total; unsigned char prefix[3]; size_t prefix_len; download_progress_t progress; void *context; } transfer_t;
static size_t write_media(void *data,size_t size,size_t count,void *context) {
    transfer_t *t=context;if(size && count>SIZE_MAX/size)return 0;size_t n=size*count;
    if(n>0xf0000000ULL-t->bytes)return 0;
    for(size_t i=0;i<n && t->prefix_len<3;i++)t->prefix[t->prefix_len++]=((unsigned char *)data)[i];
    size_t written=fwrite(data,1,n,t->file);t->bytes+=written;return written;
}
static int progress_media(void *context,curl_off_t total,curl_off_t now,curl_off_t up,curl_off_t uploaded) {
    (void)now;(void)up;(void)uploaded;transfer_t *t=context;t->total=total>0?(uint64_t)total:0;return 0;
}
static bool pump_media(void *context) {
    transfer_t *t=context;return !t->progress || t->progress(t->bytes,t->total,t->context);
}
bool download_transfer(const jfin_session_t *s,const jfin_item_details_t *d,const char *url,download_progress_t progress,void *context) {
    if(d->item.type!=JFIN_ITEM_AUDIO && d->item.type!=JFIN_ITEM_MOVIE && d->item.type!=JFIN_ITEM_EPISODE)return false;
    download_t existing;if(download_find(s,d->item.id,&existing) || cache_is_full() || download_list(s,NULL,JFIN_MAX_ITEMS)>=JFIN_MAX_ITEMS)return false;
    char key[24],part[512],path[512];download_key(s,d->item.id,key);const char *ext=download_ext(&d->item);
    if(!cache_path(key,ext,path,sizeof(path)) || !cache_part_path(key,ext,part,sizeof(part)))return false;
    transfer_t t={.progress=progress,.context=context};t.file=fopen(part,"wb");if(!t.file)return false;
    CURL *curl=curl_easy_init();if(!curl){fclose(t.file);remove(part);return false;}
    net_configure(curl);curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_media);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&t);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,15L);curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,60L);curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progress_media);curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&t);
    CURLcode result=net_perform_pumped(curl,pump_media,&t);long http=0;char *mime=NULL;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&http);curl_easy_getinfo(curl,CURLINFO_CONTENT_TYPE,&mime);
    bool ok=result==CURLE_OK && http>=200 && http<300 && t.bytes>0 && (!t.total || t.bytes==t.total) &&
        (!mime || (!strstr(mime,"text/") && !strstr(mime,"json")));
    bool container=d->item.type==JFIN_ITEM_AUDIO?
        (t.prefix_len>=3 && ((!memcmp(t.prefix,"ID3",3)) || (t.prefix[0]==0xff && (t.prefix[1]&0xe0)==0xe0))):
        (t.prefix_len>=1 && t.prefix[0]==0x47);
    ok=ok&&container;
    curl_easy_cleanup(curl);if(fflush(t.file)!=0)ok=false;if(fclose(t.file)!=0)ok=false;
    if(ok)ok=rename(part,path)==0;
    if(ok){cache_index_add(key,ext);ok=download_save(s,d);if(!ok)cache_remove(key,ext);}
    if(!ok)remove(part);
    log_write("DL: %s curl=%d HTTP=%ld bytes=%llu",ok?"saved":"failed/cancelled",(int)result,http,(unsigned long long)t.bytes);
    return ok;
}

bool download_set_resume(const jfin_session_t *s,const char *id,int64_t position){
    download_t entry;if(!download_find(s,id,&entry))return false;
    int64_t duration=entry.details.item.runtime_ticks;
    if(position<0)position=0;
    if(duration<=0 || position>=duration || (duration>0 && position>=duration-(duration/20<20000000?duration/20:20000000)))position=0;
    entry.details.resume_ticks=position;entry.details.item.resume_ticks=position;
    return download_save(s,&entry.details);
}
