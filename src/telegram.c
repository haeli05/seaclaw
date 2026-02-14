#include "telegram.h"
#include "log.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TG_API "https://api.telegram.org/bot"
#define MAX_TEXT 4096

static int is_user_allowed(const CClawConfig *cfg, long long from_id, const char *username) {
    if (!cfg->telegram_allowed[0]) return 1; /* Empty = allow all */

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", from_id);

    /* Tokenize comma-separated list */
    char allowed[1024];
    strncpy(allowed, cfg->telegram_allowed, sizeof(allowed) - 1);
    char *tok = strtok(allowed, ",");
    while (tok) {
        while (*tok == ' ') tok++;
        if (!strcmp(tok, "*")) return 1;
        if (!strcmp(tok, id_str)) return 1;
        if (username && !strcmp(tok, username)) return 1;
        tok = strtok(NULL, ",");
    }
    return 0;
}

int telegram_send(HttpClient *http, const char *token,
                  long long chat_id, const char *text) {
    char url[512];
    snprintf(url, sizeof(url), "%s%s/sendMessage", TG_API, token);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddNumberToObject(body, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(body, "text", text);
    cJSON_AddStringToObject(body, "parse_mode", "Markdown");

    char *json = cJSON_PrintUnformatted(body);
    const char *headers[] = { "Content-Type", "application/json" };
    HttpResponse resp = http_post_json(http, url, json, headers, 1);

    int ok = (resp.status >= 200 && resp.status < 300);
    if (!ok) LOG_WARN("Telegram send failed: %d", resp.status);

    http_response_free(&resp);
    free(json);
    cJSON_Delete(body);
    return ok ? 0 : -1;
}

int telegram_send_typing(HttpClient *http, const char *token, long long chat_id) {
    char url[512];
    snprintf(url, sizeof(url), "%s%s/sendChatAction", TG_API, token);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddNumberToObject(body, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(body, "action", "typing");

    char *json = cJSON_PrintUnformatted(body);
    const char *headers[] = { "Content-Type", "application/json" };
    HttpResponse resp = http_post_json(http, url, json, headers, 1);
    http_response_free(&resp);
    free(json);
    cJSON_Delete(body);
    return 0;
}

int telegram_poll_loop(HttpClient *http, const CClawConfig *cfg,
                       TelegramMsgHandler handler, void *userdata) {
    long long offset = 0;
    char url[512];

    LOG_INFO("Telegram long-polling started");

    for (;;) {
        snprintf(url, sizeof(url), "%s%s/getUpdates?timeout=30&offset=%lld",
                 TG_API, cfg->telegram_token, offset);

        const char *headers[] = { "Content-Type", "application/json" };
        HttpResponse resp = http_get(http, url, headers, 1);

        if (!resp.body) {
            LOG_WARN("Telegram poll: no response, retrying...");
            http_response_free(&resp);
            continue;
        }

        cJSON *root = cJSON_Parse(resp.body);
        http_response_free(&resp);

        if (!root) continue;

        cJSON *ok_field = cJSON_GetObjectItem(root, "ok");
        if (!ok_field || !cJSON_IsTrue(ok_field)) {
            LOG_WARN("Telegram API error");
            cJSON_Delete(root);
            continue;
        }

        cJSON *result = cJSON_GetObjectItem(root, "result");
        if (!result || !cJSON_IsArray(result)) {
            cJSON_Delete(root);
            continue;
        }

        cJSON *update;
        cJSON_ArrayForEach(update, result) {
            cJSON *uid = cJSON_GetObjectItem(update, "update_id");
            if (uid) offset = (long long)uid->valuedouble + 1;

            cJSON *message = cJSON_GetObjectItem(update, "message");
            if (!message) continue;

            cJSON *text = cJSON_GetObjectItem(message, "text");
            if (!text || !text->valuestring) continue;

            cJSON *from = cJSON_GetObjectItem(message, "from");
            cJSON *chat = cJSON_GetObjectItem(message, "chat");
            if (!chat) continue;

            long long chat_id = (long long)cJSON_GetObjectItem(chat, "id")->valuedouble;
            long long from_id = from ? (long long)cJSON_GetObjectItem(from, "id")->valuedouble : 0;
            cJSON *uname = from ? cJSON_GetObjectItem(from, "username") : NULL;
            const char *username = (uname && uname->valuestring) ? uname->valuestring : "";
            int msg_id = (int)cJSON_GetObjectItem(message, "message_id")->valuedouble;

            if (!is_user_allowed(cfg, from_id, username)) {
                LOG_WARN("Blocked Telegram user: %lld (%s)", from_id, username);
                continue;
            }

            LOG_INFO("Telegram [%s]: %s",
                     username[0] ? username : "unknown",
                     strlen(text->valuestring) > 80 ? "(long message)" : text->valuestring);

            /* Send typing indicator */
            telegram_send_typing(http, cfg->telegram_token, chat_id);

            TelegramMessage tmsg = {
                .chat_id = chat_id,
                .message_id = msg_id,
                .text = text->valuestring,
                .from_username = (char *)username,
                .from_id = from_id,
            };

            char *reply = handler(&tmsg, userdata);
            if (reply && reply[0]) {
                telegram_send(http, cfg->telegram_token, chat_id, reply);
                free(reply);
            }
        }

        cJSON_Delete(root);
    }

    return 0;
}
