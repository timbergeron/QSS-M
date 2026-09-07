/* Shared, cancellable libcurl transfers. */
#ifndef QUAKE_NET_CURL_H
#define QUAKE_NET_CURL_H

#include <curl/curl.h>

/* Like curl_easy_perform, retaining the caller's options and callbacks.
 * The caller still owns the easy handle and must clean it up. */
CURLcode NET_CurlEasyPerform(CURL *curl);

#endif
