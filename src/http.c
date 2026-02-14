#include "http.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

struct HttpClient {
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt         cacert;
    mbedtls_ssl_config       ssl_conf;
};

HttpClient *http_client_new(void) {
    HttpClient *c = calloc(1, sizeof(*c));
    mbedtls_entropy_init(&c->entropy);
    mbedtls_ctr_drbg_init(&c->drbg);
    mbedtls_x509_crt_init(&c->cacert);
    mbedtls_ssl_config_init(&c->ssl_conf);

    mbedtls_ctr_drbg_seed(&c->drbg, mbedtls_entropy_func,
                          &c->entropy, (const unsigned char *)"cclaw", 5);

    /* Load system CA certs */
    mbedtls_x509_crt_parse_path(&c->cacert, "/etc/ssl/certs");

    mbedtls_ssl_config_defaults(&c->ssl_conf,
                                MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_STREAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(&c->ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&c->ssl_conf, &c->cacert, NULL);
    mbedtls_ssl_conf_rng(&c->ssl_conf, mbedtls_ctr_drbg_random, &c->drbg);

    return c;
}

void http_client_free(HttpClient *c) {
    if (!c) return;
    mbedtls_ssl_config_free(&c->ssl_conf);
    mbedtls_x509_crt_free(&c->cacert);
    mbedtls_ctr_drbg_free(&c->drbg);
    mbedtls_entropy_free(&c->entropy);
    free(c);
}

/* Parse URL into host, port, path. */
static int parse_url(const char *url, char *host, size_t hlen,
                     char *port, size_t plen, char *path, size_t pathlen) {
    if (strncmp(url, "https://", 8) != 0) return -1;
    const char *hp = url + 8;
    const char *slash = strchr(hp, '/');
    const char *colon = strchr(hp, ':');

    if (colon && (!slash || colon < slash)) {
        size_t n = (size_t)(colon - hp);
        if (n >= hlen) return -1;
        memcpy(host, hp, n); host[n] = '\0';
        const char *pe = slash ? slash : hp + strlen(hp);
        n = (size_t)(pe - colon - 1);
        if (n >= plen) return -1;
        memcpy(port, colon + 1, n); port[n] = '\0';
    } else {
        size_t n = slash ? (size_t)(slash - hp) : strlen(hp);
        if (n >= hlen) return -1;
        memcpy(host, hp, n); host[n] = '\0';
        strncpy(port, "443", plen - 1);
    }

    if (slash) strncpy(path, slash, pathlen - 1);
    else       strncpy(path, "/", pathlen - 1);

    return 0;
}

/* Open TLS connection, send request, return SSL context. */
static int tls_connect(HttpClient *c, const char *host, const char *port,
                       mbedtls_net_context *net, mbedtls_ssl_context *ssl) {
    mbedtls_net_init(net);
    mbedtls_ssl_init(ssl);

    int ret = mbedtls_net_connect(net, host, port, MBEDTLS_NET_PROTO_TCP);
    if (ret) {
        LOG_ERROR("TCP connect to %s:%s failed: %d", host, port, ret);
        return ret;
    }

    mbedtls_ssl_setup(ssl, &c->ssl_conf);
    mbedtls_ssl_set_hostname(ssl, host);
    mbedtls_ssl_set_bio(ssl, net, mbedtls_net_send, mbedtls_net_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char errbuf[128];
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            LOG_ERROR("TLS handshake failed: %s", errbuf);
            return ret;
        }
    }

    return 0;
}

/* Build HTTP request string. */
static char *build_request(const char *method, const char *host,
                           const char *path, const char *body,
                           const char **headers, int num_headers) {
    size_t cap = 4096 + (body ? strlen(body) : 0);
    char *req = malloc(cap);
    int off = snprintf(req, cap, "%s %s HTTP/1.1\r\nHost: %s\r\n", method, path, host);

    for (int i = 0; i < num_headers * 2; i += 2) {
        off += snprintf(req + off, cap - (size_t)off, "%s: %s\r\n", headers[i], headers[i+1]);
    }

    if (body) {
        off += snprintf(req + off, cap - (size_t)off,
                        "Content-Length: %zu\r\nContent-Type: application/json\r\n\r\n%s",
                        strlen(body), body);
    } else {
        off += snprintf(req + off, cap - (size_t)off, "\r\n");
    }
    (void)off;
    return req;
}

/* Read full HTTP response (non-streaming). */
static HttpResponse read_response(mbedtls_ssl_context *ssl) {
    HttpResponse resp = {0};
    char buf[8192];
    char *raw = NULL;
    size_t raw_len = 0, raw_cap = 0;

    for (;;) {
        int n = mbedtls_ssl_read(ssl, (unsigned char *)buf, sizeof(buf));
        if (n == MBEDTLS_ERR_SSL_WANT_READ) continue;
        if (n <= 0) break;

        if (raw_len + (size_t)n > raw_cap) {
            raw_cap = (raw_cap + (size_t)n) * 2;
            raw = realloc(raw, raw_cap);
        }
        memcpy(raw + raw_len, buf, (size_t)n);
        raw_len += (size_t)n;
    }

    if (!raw) return resp;

    /* Parse status line */
    char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        /* Parse status code */
        char *sp = strchr(raw, ' ');
        if (sp) resp.status = atoi(sp + 1);

        resp.body_len = raw_len - (size_t)(body_start - raw);
        resp.body = malloc(resp.body_len + 1);
        memcpy(resp.body, body_start, resp.body_len);
        resp.body[resp.body_len] = '\0';
    }

    free(raw);
    return resp;
}

HttpResponse http_post_json(HttpClient *c, const char *url, const char *body,
                            const char **headers, int num_headers) {
    HttpResponse resp = {0};
    char host[256], port[8], path[1024];
    if (parse_url(url, host, sizeof(host), port, sizeof(port), path, sizeof(path))) {
        LOG_ERROR("Invalid URL: %s", url);
        return resp;
    }

    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    if (tls_connect(c, host, port, &net, &ssl)) return resp;

    char *req = build_request("POST", host, path, body, headers, num_headers);
    mbedtls_ssl_write(&ssl, (unsigned char *)req, strlen(req));
    free(req);

    resp = read_response(&ssl);

    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_net_free(&net);

    return resp;
}

HttpResponse http_get(HttpClient *c, const char *url,
                      const char **headers, int num_headers) {
    HttpResponse resp = {0};
    char host[256], port[8], path[1024];
    if (parse_url(url, host, sizeof(host), port, sizeof(port), path, sizeof(path))) return resp;

    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    if (tls_connect(c, host, port, &net, &ssl)) return resp;

    char *req = build_request("GET", host, path, NULL, headers, num_headers);
    mbedtls_ssl_write(&ssl, (unsigned char *)req, strlen(req));
    free(req);

    resp = read_response(&ssl);

    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_net_free(&net);

    return resp;
}

int http_post_stream(HttpClient *c, const char *url, const char *body,
                     const char **headers, int num_headers,
                     HttpStreamCb cb, void *userdata) {
    char host[256], port[8], path[1024];
    if (parse_url(url, host, sizeof(host), port, sizeof(port), path, sizeof(path))) return -1;

    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    if (tls_connect(c, host, port, &net, &ssl)) return -1;

    char *req = build_request("POST", host, path, body, headers, num_headers);
    mbedtls_ssl_write(&ssl, (unsigned char *)req, strlen(req));
    free(req);

    /* Read headers first */
    char hdr_buf[4096];
    size_t hdr_len = 0;
    bool headers_done = false;

    while (!headers_done) {
        int n = mbedtls_ssl_read(&ssl, (unsigned char *)hdr_buf + hdr_len, sizeof(hdr_buf) - hdr_len);
        if (n <= 0) break;
        hdr_len += (size_t)n;
        if (strstr(hdr_buf, "\r\n\r\n")) headers_done = true;
    }

    /* Stream SSE data: lines */
    char line_buf[16384];
    size_t line_len = 0;

    /* Check if we have leftover data after headers */
    char *body_start = strstr(hdr_buf, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        size_t leftover = hdr_len - (size_t)(body_start - hdr_buf);
        if (leftover > 0) {
            memcpy(line_buf, body_start, leftover);
            line_len = leftover;
        }
    }

    for (;;) {
        /* Process complete lines */
        char *nl;
        while ((nl = memchr(line_buf, '\n', line_len)) != NULL) {
            size_t llen = (size_t)(nl - line_buf);
            /* Remove \r if present */
            if (llen > 0 && line_buf[llen-1] == '\r') llen--;
            line_buf[llen] = '\0';

            /* SSE: data: prefix */
            if (strncmp(line_buf, "data: ", 6) == 0) {
                if (!cb(line_buf + 6, llen - 6, userdata)) {
                    goto done;
                }
            }

            /* Shift buffer */
            size_t consumed = (size_t)(nl - line_buf) + 1;
            memmove(line_buf, nl + 1, line_len - consumed);
            line_len -= consumed;
        }

        int n = mbedtls_ssl_read(&ssl, (unsigned char *)line_buf + line_len,
                                  sizeof(line_buf) - line_len - 1);
        if (n == MBEDTLS_ERR_SSL_WANT_READ) continue;
        if (n <= 0) break;
        line_len += (size_t)n;
    }

done:
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_net_free(&net);

    return 0;
}

void http_response_free(HttpResponse *r) {
    free(r->body);
    free(r->headers);
    r->body = NULL;
    r->headers = NULL;
}
