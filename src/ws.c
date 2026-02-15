/*
 * Minimal WebSocket server (RFC 6455).
 *
 * Supports text frames, ping/pong, close handshake, and masked client frames.
 * Uses plain TCP sockets with poll() for multiplexing.
 */

#include "ws.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <pthread.h>

/* mbedtls for SHA-1 (WebSocket handshake requires it) */
#include "mbedtls/sha1.h"

#define WS_MAGIC "258EAFA5-E914-47DA-95CA-5AB9DC085B7"
#define MAX_CLIENTS 64
#define READ_BUF_SIZE 65536

/* Base64 encode (minimal, for handshake only) */
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const unsigned char *in, size_t len, char *out) {
    size_t i, j = 0;
    for (i = 0; i + 2 < len; i += 3) {
        out[j++] = b64_table[(in[i] >> 2) & 0x3F];
        out[j++] = b64_table[((in[i] & 0x3) << 4) | (in[i+1] >> 4)];
        out[j++] = b64_table[((in[i+1] & 0xF) << 2) | (in[i+2] >> 6)];
        out[j++] = b64_table[in[i+2] & 0x3F];
    }
    if (i < len) {
        out[j++] = b64_table[(in[i] >> 2) & 0x3F];
        if (i + 1 < len) {
            out[j++] = b64_table[((in[i] & 0x3) << 4) | (in[i+1] >> 4)];
            out[j++] = b64_table[(in[i+1] & 0xF) << 2];
        } else {
            out[j++] = b64_table[(in[i] & 0x3) << 4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    out[j] = '\0';
}

/* Find header value in raw HTTP request. */
static const char *find_header(const char *req, const char *name, char *buf, size_t buf_len) {
    const char *p = req;
    size_t nlen = strlen(name);
    while ((p = strstr(p, name)) != NULL) {
        /* Check it's at start of line */
        if (p != req && *(p-1) != '\n') { p += nlen; continue; }
        p += nlen;
        while (*p == ' ' || *p == ':') p++;
        const char *end = strstr(p, "\r\n");
        if (!end) end = p + strlen(p);
        size_t vlen = (size_t)(end - p);
        if (vlen >= buf_len) vlen = buf_len - 1;
        memcpy(buf, p, vlen);
        buf[vlen] = '\0';
        return buf;
    }
    return NULL;
}

int ws_handshake(int client_fd, const char *auth_token) {
    char buf[4096];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';

    /* Verify it's a WebSocket upgrade request */
    if (!strstr(buf, "Upgrade: websocket") && !strstr(buf, "Upgrade: WebSocket")) {
        LOG_WARN("WS handshake: not a WebSocket upgrade request");
        return -1;
    }

    /* Optional auth token check (via query param or header) */
    if (auth_token && auth_token[0]) {
        char token_buf[256];
        bool authed = false;

        /* Check Authorization header */
        if (find_header(buf, "Authorization", token_buf, sizeof(token_buf))) {
            if (strncmp(token_buf, "Bearer ", 7) == 0 && !strcmp(token_buf + 7, auth_token))
                authed = true;
        }

        /* Check ?token= query param */
        if (!authed) {
            char *qmark = strchr(buf, '?');
            if (qmark) {
                char *tok = strstr(qmark, "token=");
                if (tok) {
                    tok += 6;
                    char *end = tok;
                    while (*end && *end != ' ' && *end != '&' && *end != '\r') end++;
                    size_t tlen = (size_t)(end - tok);
                    if (tlen == strlen(auth_token) && !strncmp(tok, auth_token, tlen))
                        authed = true;
                }
            }
        }

        if (!authed) {
            const char *resp = "HTTP/1.1 401 Unauthorized\r\n\r\n";
            write(client_fd, resp, strlen(resp));
            return -1;
        }
    }

    /* Extract Sec-WebSocket-Key */
    char ws_key[128];
    if (!find_header(buf, "Sec-WebSocket-Key", ws_key, sizeof(ws_key))) {
        LOG_WARN("WS handshake: no Sec-WebSocket-Key");
        return -1;
    }

    /* Compute accept key: SHA1(key + magic), base64 encoded */
    char concat[256];
    snprintf(concat, sizeof(concat), "%s%s", ws_key, WS_MAGIC);

    unsigned char sha1_hash[20];
    mbedtls_sha1((const unsigned char *)concat, strlen(concat), sha1_hash);

    char accept_key[64];
    base64_encode(sha1_hash, 20, accept_key);

    /* Send response */
    char response[512];
    int rlen = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept_key);

    if (write(client_fd, response, (size_t)rlen) != rlen) return -1;

    return 0;
}

int ws_read_frame(int fd, WsFrame *frame) {
    unsigned char hdr[2];
    ssize_t n = read(fd, hdr, 2);
    if (n != 2) return -1;

    frame->fin = (hdr[0] & 0x80) != 0;
    frame->opcode = (WsOpcode)(hdr[0] & 0x0F);
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t payload_len = hdr[1] & 0x7F;

    if (payload_len == 126) {
        unsigned char ext[2];
        if (read(fd, ext, 2) != 2) return -1;
        payload_len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        unsigned char ext[8];
        if (read(fd, ext, 8) != 8) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | ext[i];
    }

    unsigned char mask[4] = {0};
    if (masked) {
        if (read(fd, mask, 4) != 4) return -1;
    }

    frame->payload_len = (size_t)payload_len;
    frame->payload = malloc(frame->payload_len + 1);

    size_t total = 0;
    while (total < frame->payload_len) {
        n = read(fd, frame->payload + total, frame->payload_len - total);
        if (n <= 0) { free(frame->payload); frame->payload = NULL; return -1; }
        total += (size_t)n;
    }

    /* Unmask */
    if (masked) {
        for (size_t i = 0; i < frame->payload_len; i++)
            frame->payload[i] ^= (char)mask[i % 4];
    }
    frame->payload[frame->payload_len] = '\0';

    return 0;
}

int ws_write_frame(int fd, WsOpcode opcode, const char *data, size_t len) {
    unsigned char hdr[10];
    size_t hdr_len = 2;

    hdr[0] = 0x80 | (unsigned char)opcode;  /* FIN + opcode */

    if (len < 126) {
        hdr[1] = (unsigned char)len;
    } else if (len < 65536) {
        hdr[1] = 126;
        hdr[2] = (unsigned char)(len >> 8);
        hdr[3] = (unsigned char)(len & 0xFF);
        hdr_len = 4;
    } else {
        hdr[1] = 127;
        for (int i = 0; i < 8; i++)
            hdr[2 + i] = (unsigned char)(len >> (56 - i * 8));
        hdr_len = 10;
    }

    if (write(fd, hdr, hdr_len) != (ssize_t)hdr_len) return -1;
    if (len > 0 && write(fd, data, len) != (ssize_t)len) return -1;

    return 0;
}

int ws_send_text(int client_fd, const char *msg, size_t len) {
    return ws_write_frame(client_fd, WS_OP_TEXT, msg, len);
}

int ws_send_close(int client_fd) {
    return ws_write_frame(client_fd, WS_OP_CLOSE, NULL, 0);
}

int ws_server_start(const WsServerConfig *cfg) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR("WS: socket() failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)cfg->port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("WS: bind() on port %d failed: %s", cfg->port, strerror(errno));
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 16) < 0) {
        LOG_ERROR("WS: listen() failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    LOG_INFO("WebSocket server listening on port %d", cfg->port);

    /* Poll-based event loop */
    struct pollfd fds[MAX_CLIENTS + 1];
    int nfds = 1;

    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    for (;;) {
        int ret = poll(fds, (nfds_t)nfds, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("WS: poll() failed: %s", strerror(errno));
            break;
        }

        /* Accept new connections */
        if (fds[0].revents & POLLIN) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd >= 0) {
                if (ws_handshake(client_fd, cfg->auth_token) == 0) {
                    if (nfds < MAX_CLIENTS + 1) {
                        fds[nfds].fd = client_fd;
                        fds[nfds].events = POLLIN;
                        nfds++;
                        LOG_INFO("WS: client connected (fd=%d, total=%d)", client_fd, nfds - 1);
                        if (cfg->on_connect) cfg->on_connect(client_fd, cfg->userdata);
                    } else {
                        LOG_WARN("WS: max clients reached, rejecting");
                        ws_send_close(client_fd);
                        close(client_fd);
                    }
                } else {
                    close(client_fd);
                }
            }
        }

        /* Handle client messages */
        for (int i = 1; i < nfds; i++) {
            if (!(fds[i].revents & POLLIN)) continue;

            WsFrame frame;
            if (ws_read_frame(fds[i].fd, &frame) != 0) {
                /* Connection closed or error */
                goto remove_client;
            }

            switch (frame.opcode) {
            case WS_OP_TEXT:
                if (cfg->on_message) {
                    if (!cfg->on_message(fds[i].fd, frame.payload, frame.payload_len, cfg->userdata)) {
                        free(frame.payload);
                        ws_send_close(fds[i].fd);
                        goto remove_client;
                    }
                }
                break;
            case WS_OP_PING:
                ws_write_frame(fds[i].fd, WS_OP_PONG, frame.payload, frame.payload_len);
                break;
            case WS_OP_CLOSE:
                ws_send_close(fds[i].fd);
                free(frame.payload);
                goto remove_client;
            default:
                break;
            }

            free(frame.payload);
            continue;

        remove_client:
            LOG_INFO("WS: client disconnected (fd=%d)", fds[i].fd);
            if (cfg->on_disconnect) cfg->on_disconnect(fds[i].fd, cfg->userdata);
            close(fds[i].fd);
            fds[i] = fds[nfds - 1];
            nfds--;
            i--;  /* Re-check this slot */
        }
    }

    close(server_fd);
    return 0;
}
