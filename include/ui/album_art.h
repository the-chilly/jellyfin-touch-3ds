#ifndef JFIN_ALBUM_ART_H
#define JFIN_ALBUM_ART_H
#include <stdbool.h>
#include "api/jellyfin.h"
/* Background JPEG/metadata/home loader. GPU uploads stay on the main thread. */
bool album_art_init(void);
void album_art_update(void); /* inside a synchronized C3D frame, before drawing */
bool album_art_load_cached(const jfin_session_t *session,const jfin_item_t *item);
void album_art_request_cached(const jfin_session_t *session,const jfin_item_t *item);
void album_art_request(const jfin_session_t *session, const jfin_item_t *item);
bool album_art_draw_item(const jfin_session_t *session, const jfin_item_t *item,
                         float x, float y, float width, float height);
void album_art_request_details(const jfin_session_t *session, const char *item_id);
bool album_art_take_details(const char *item_id, jfin_item_details_t *out, bool *success);
void album_art_request_home(const jfin_session_t *session);
bool album_art_take_home(int row, jfin_item_list_t *out, bool *success);
bool album_art_load(const jfin_session_t *session, const jfin_item_t *item);
void album_art_draw(float x, float y, float size);
bool album_art_is_loaded(void);
void album_art_request_stop(void);
void album_art_cleanup(void);
#endif
