#ifndef JFIN_NET_H
#define JFIN_NET_H
#include <curl/curl.h>
#include <stdbool.h>
#ifndef NET_CA_PATH
#define NET_CA_PATH "sdmc:/3ds/jellyfin-3ds/cacert.pem"
#endif
/* Apply the same verified TLS policy to API, artwork and media transfers. */
CURLcode net_perform_pumped(CURL *curl, bool (*pump)(void *), void *context);
void net_configure(CURL *curl);
/* Cancel must be accessed atomically. No UI callbacks from worker transfers. */
CURLcode net_perform_cancelable(CURL *curl, const bool *cancel);
#endif
