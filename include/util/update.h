#ifndef JFIN_UPDATE_H
#define JFIN_UPDATE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define UPDATE_REPO "the-chilly/jellyfin-touch-3ds"
#define UPDATE_API "https://api.github.com/repos/" UPDATE_REPO "/releases/latest"
#define UPDATE_APP_PATH "sdmc:/3ds/jellyfin-3ds/jellyfin-3ds.3dsx"
typedef struct { char version[32], url[512], sha256[65]; uint64_t size; bool available; } update_info_t;
typedef bool (*update_progress_t)(uint64_t bytes,uint64_t total,void *context);
bool update_parse_release(const char *json,const char *current,update_info_t *out);
bool update_check(update_info_t *out,update_progress_t progress,void *context,char *message,size_t length);
bool update_install(const update_info_t *info,update_progress_t progress,void *context,char *message,size_t length);
#endif
