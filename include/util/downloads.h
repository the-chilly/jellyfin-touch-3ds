#ifndef JFIN_DOWNLOADS_H
#define JFIN_DOWNLOADS_H
#include "api/jellyfin.h"
#include <stddef.h>
typedef struct { jfin_item_details_t details; char key[24]; uint64_t bytes; } download_t;
void download_key(const jfin_session_t *session, const char *id, char key[24]);
const char *download_ext(const jfin_item_t *item);
typedef bool (*download_progress_t)(uint64_t bytes, uint64_t total, void *context);
/* Only commits nonempty, successful media responses; failed partials are removed. */
bool download_transfer(const jfin_session_t *session, const jfin_item_details_t *details, const char *url, download_progress_t progress, void *context);
bool download_save(const jfin_session_t *session, const jfin_item_details_t *details);
int download_list(const jfin_session_t *session, download_t *out, int capacity);
bool download_find(const jfin_session_t *session, const char *id, download_t *out);
bool download_set_resume(const jfin_session_t *session,const char *id,int64_t position);
bool download_delete(const download_t *item);
#endif
