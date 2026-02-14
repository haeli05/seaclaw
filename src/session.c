#include "session.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Session *session_new(const char *workspace, const char *session_id) {
    Session *s = calloc(1, sizeof(*s));
    s->messages = cJSON_CreateArray();
    s->count = 0;

    if (session_id) {
        snprintf(s->session_file, sizeof(s->session_file),
                 "%s/.cclaw/sessions/%s.json", workspace, session_id);
    }

    /* Try to load existing session */
    if (s->session_file[0]) {
        FILE *fp = fopen(s->session_file, "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char *buf = malloc((size_t)size + 1);
            size_t n = fread(buf, 1, (size_t)size, fp);
            buf[n] = '\0';
            fclose(fp);

            cJSON *loaded = cJSON_Parse(buf);
            free(buf);
            if (loaded && cJSON_IsArray(loaded)) {
                cJSON_Delete(s->messages);
                s->messages = loaded;
                s->count = cJSON_GetArraySize(loaded);
                LOG_DEBUG("Loaded session %s (%d messages)", session_id, s->count);
            } else {
                cJSON_Delete(loaded);
            }
        }
    }

    return s;
}

void session_add_user(Session *s, const char *text) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", text);
    cJSON_AddItemToArray(s->messages, msg);
    s->count++;
}

void session_add_assistant(Session *s, const char *text) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "assistant");
    /* Anthropic format: content is an array of blocks */
    cJSON *content = cJSON_AddArrayToObject(msg, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", text);
    cJSON_AddItemToArray(content, block);
    cJSON_AddItemToArray(s->messages, msg);
    s->count++;
}

void session_add_tool_use(Session *s, const char *tool_id, const char *name, const char *input_json) {
    /* Check if last message is assistant — append tool_use block to it */
    cJSON *last = cJSON_GetArrayItem(s->messages, s->count - 1);
    cJSON *role = last ? cJSON_GetObjectItem(last, "role") : NULL;

    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "tool_use");
    cJSON_AddStringToObject(block, "id", tool_id);
    cJSON_AddStringToObject(block, "name", name);
    cJSON *input = cJSON_Parse(input_json);
    if (input) cJSON_AddItemToObject(block, "input", input);
    else cJSON_AddItemToObject(block, "input", cJSON_CreateObject());

    if (role && !strcmp(role->valuestring, "assistant")) {
        /* Append to existing assistant content array */
        cJSON *content = cJSON_GetObjectItem(last, "content");
        if (content && cJSON_IsArray(content)) {
            cJSON_AddItemToArray(content, block);
            return;
        }
    }

    /* Create new assistant message with tool_use */
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "assistant");
    cJSON *content = cJSON_AddArrayToObject(msg, "content");
    cJSON_AddItemToArray(content, block);
    cJSON_AddItemToArray(s->messages, msg);
    s->count++;
}

void session_add_tool_result(Session *s, const char *tool_id, const char *output) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON *content = cJSON_AddArrayToObject(msg, "content");
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "tool_result");
    cJSON_AddStringToObject(block, "tool_use_id", tool_id);
    cJSON_AddStringToObject(block, "content", output);
    cJSON_AddItemToArray(content, block);
    cJSON_AddItemToArray(s->messages, msg);
    s->count++;
}

char *session_messages_json(Session *s) {
    return cJSON_PrintUnformatted(s->messages);
}

void session_save(Session *s) {
    if (!s->session_file[0]) return;

    /* Create directory */
    char dir[512];
    strncpy(dir, s->session_file, sizeof(dir) - 1);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        char cmd[600];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", dir);
        system(cmd);
    }

    char *json = cJSON_Print(s->messages);
    FILE *fp = fopen(s->session_file, "w");
    if (fp) {
        fputs(json, fp);
        fclose(fp);
    }
    free(json);
}

void session_free(Session *s) {
    if (!s) return;
    cJSON_Delete(s->messages);
    free(s);
}
