#ifndef CCLAW_HTTP_H
#define CCLAW_HTTP_H

#include <stddef.h>
#include <stdbool.h>

typedef struct HttpClient HttpClient;

typedef struct {
    int  status;
    char *body;
    size_t body_len;
    char *headers;
} HttpResponse;

/* Callback for streaming responses. Return false to abort. */
typedef bool (*HttpStreamCb)(const char *chunk, size_t len, void *userdata);

/* Create/destroy client (holds TLS context). */
HttpClient *http_client_new(void);
void http_client_free(HttpClient *c);

/* POST with JSON body. Caller must free response body. */
HttpResponse http_post_json(HttpClient *c,
                            const char *url,
                            const char *body,
                            const char **headers,  /* NULL-terminated key,val pairs */
                            int num_headers);

/* POST with streaming SSE response. Calls cb for each data: line. */
int http_post_stream(HttpClient *c,
                     const char *url,
                     const char *body,
                     const char **headers,
                     int num_headers,
                     HttpStreamCb cb,
                     void *userdata);

/* Simple GET. */
HttpResponse http_get(HttpClient *c, const char *url,
                      const char **headers, int num_headers);

void http_response_free(HttpResponse *r);

#endif
