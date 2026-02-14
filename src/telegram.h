#ifndef CCLAW_TELEGRAM_H
#define CCLAW_TELEGRAM_H

#include "http.h"
#include "config.h"

/* Incoming Telegram message */
typedef struct {
    long long chat_id;
    int       message_id;
    char     *text;
    char     *from_username;
    long long from_id;
} TelegramMessage;

/* Callback when a message arrives. Return the reply text (or NULL). */
typedef char *(*TelegramMsgHandler)(const TelegramMessage *msg, void *userdata);

/* Start Telegram long-polling loop (blocking). */
int telegram_poll_loop(HttpClient *http, const CClawConfig *cfg,
                       TelegramMsgHandler handler, void *userdata);

/* Send a text message. */
int telegram_send(HttpClient *http, const char *token,
                  long long chat_id, const char *text);

/* Send typing indicator. */
int telegram_send_typing(HttpClient *http, const char *token, long long chat_id);

#endif
