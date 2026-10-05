#include "util/net.h"

void net_configure(CURL *curl)
{
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, NET_CA_PATH);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    /* Tokens occur in media query strings: don't send them to a redirect host.
     * Use the server's final URL, including any reverse-proxy base path. */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
}

/* easy_perform can wait indefinitely for the next streaming byte. Polling the
 * same transfer allows stop requests to interrupt an idle connection safely. */
CURLcode net_perform_pumped(CURL *curl, bool (*pump)(void *), void *context)
{
    if (!pump(context)) return CURLE_ABORTED_BY_CALLBACK;
    CURLM *multi = curl_multi_init();
    if (!multi) return CURLE_FAILED_INIT;
    if (curl_multi_add_handle(multi,curl)!=CURLM_OK) { curl_multi_cleanup(multi); return CURLE_FAILED_INIT; }
    int running=0;CURLMcode status=curl_multi_perform(multi,&running);
    bool stopped=false;
    while(status==CURLM_OK && running) {
        if (!pump(context)) { stopped=true;break; }
        status=curl_multi_poll(multi,NULL,0,50,NULL);
        if(status==CURLM_OK)status=curl_multi_perform(multi,&running);
    }
    CURLcode result=stopped ? CURLE_ABORTED_BY_CALLBACK : CURLE_RECV_ERROR;
    int pending=0;CURLMsg *message;
    while(!stopped && (message=curl_multi_info_read(multi,&pending)))
        if(message->msg==CURLMSG_DONE && message->easy_handle==curl) result=message->data.result;
    curl_multi_remove_handle(multi,curl);curl_multi_cleanup(multi);
    return result;
}

static bool keep_running(void *context) { return !__atomic_load_n((const bool *)context,__ATOMIC_ACQUIRE); }
CURLcode net_perform_cancelable(CURL *curl,const bool *cancel) { return net_perform_pumped(curl,keep_running,(void *)cancel); }
