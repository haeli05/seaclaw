#ifndef CCLAW_WS_H
#define CCLAW_WS_H

#include <stdbool.h>
#include <stddef.h>

/* WebSocket opcode */
typedef enum {
    WS_OP_TEXT   = 0x1,
    WS_OP_BINARY = 0x2,
    WS_OP_CLOSE  = 0x8,
    WS_OP_PING   = 0x9,
    WS_OP_PONG   = 0xA,
} WsOpcode;

/* WebSocket frame (decoded) */
typedef struct {
    WsOpcode opcode;
    char    *payload;
    size_t   payload_len;
    bool     fin;
} WsFrame;

/* WebSocket message callback. Return false to close connection. */
typedef bool (*WsMessageCb)(int client_fd, const char *msg, size_t len, void *userdata);

/* WebSocket connection callback (new client). */
typedef void (*WsConnectCb)(int client_fd, void *userdata);

/* WebSocket disconnect callback. */
typedef void (*WsDisconnectCb)(int client_fd, void *userdata);

/* WebSocket server config */
typedef struct {
    int            port;
    const char    *auth_token;     /* Optional: reject connections without this token */
    WsMessageCb    on_message;
    WsConnectCb    on_connect;
    WsDisconnectCb on_disconnect;
    void          *userdata;
} WsServerConfig;

/* Start WebSocket server (blocking — run in a thread). */
int ws_server_start(const WsServerConfig *cfg);

/* Send a text message to a connected client. */
int ws_send_text(int client_fd, const char *msg, size_t len);

/* Send a close frame to a client. */
int ws_send_close(int client_fd);

/* Perform WebSocket handshake on an accepted socket.
 * Returns 0 on success, -1 on failure. */
int ws_handshake(int client_fd, const char *auth_token);

/* Read one WebSocket frame. Caller frees frame.payload. */
int ws_read_frame(int fd, WsFrame *frame);

/* Write a WebSocket frame. */
int ws_write_frame(int fd, WsOpcode opcode, const char *data, size_t len);

#endif
