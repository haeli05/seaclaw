#include "provider.h"
#include "log.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ANTHROPIC_URL "https://api.anthropic.com/v1/messages"
#define MAX_TOKENS 8192

/* Build the request body JSON for Anthropic Messages API. */
static char *build_request_body(const char *model, const char *system_prompt,
                                const char *messages_json, const char *tools_json,
                                float temperature, bool stream) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddNumberToObject(root, "max_tokens", MAX_TOKENS);
    cJSON_AddNumberToObject(root, "temperature", (double)temperature);

    if (stream) cJSON_AddBoolToObject(root, "stream", 1);

    if (system_prompt && system_prompt[0]) {
        cJSON_AddStringToObject(root, "system", system_prompt);
    }

    /* Messages: parse the pre-built JSON array */
    cJSON *msgs = cJSON_Parse(messages_json);
    if (msgs) {
        cJSON_AddItemToObject(root, "messages", msgs);
    } else {
        /* Fallback: single user message */
        cJSON *arr = cJSON_AddArrayToObject(root, "messages");
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", "user");
        cJSON_AddStringToObject(msg, "content", messages_json);
        cJSON_AddItemToArray(arr, msg);
    }

    if (tools_json && tools_json[0]) {
        cJSON *tools = cJSON_Parse(tools_json);
        if (tools) cJSON_AddItemToObject(root, "tools", tools);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

/* Parse Anthropic non-streaming response. */
static ChatResponse parse_response(const char *json_str) {
    ChatResponse resp = {0};
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERROR("Failed to parse API response");
        resp.text = strdup("Error: failed to parse API response");
        return resp;
    }

    /* Check for error */
    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (err) {
        cJSON *msg = cJSON_GetObjectItem(err, "message");
        resp.text = strdup(msg ? msg->valuestring : "Unknown API error");
        cJSON_Delete(root);
        return resp;
    }

    /* Stop reason */
    cJSON *stop = cJSON_GetObjectItem(root, "stop_reason");
    if (stop && stop->valuestring) resp.stop_reason = strdup(stop->valuestring);

    /* Usage */
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *in_tok = cJSON_GetObjectItem(usage, "input_tokens");
        cJSON *out_tok = cJSON_GetObjectItem(usage, "output_tokens");
        if (in_tok) resp.input_tokens = in_tok->valueint;
        if (out_tok) resp.output_tokens = out_tok->valueint;
    }

    /* Content blocks */
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (content && cJSON_IsArray(content)) {
        /* Count tool uses */
        int num_tools = 0;
        int text_len = 0;
        cJSON *block;
        cJSON_ArrayForEach(block, content) {
            cJSON *type = cJSON_GetObjectItem(block, "type");
            if (!type) continue;
            if (!strcmp(type->valuestring, "tool_use")) num_tools++;
            if (!strcmp(type->valuestring, "text")) {
                cJSON *t = cJSON_GetObjectItem(block, "text");
                if (t && t->valuestring) text_len += (int)strlen(t->valuestring);
            }
        }

        if (num_tools > 0) {
            resp.tool_calls = calloc((size_t)num_tools, sizeof(ToolCall));
            resp.num_tools = num_tools;
            int ti = 0;
            cJSON_ArrayForEach(block, content) {
                cJSON *type = cJSON_GetObjectItem(block, "type");
                if (!type || strcmp(type->valuestring, "tool_use")) continue;
                cJSON *id = cJSON_GetObjectItem(block, "id");
                cJSON *name = cJSON_GetObjectItem(block, "name");
                cJSON *input = cJSON_GetObjectItem(block, "input");
                if (id) resp.tool_calls[ti].id = strdup(id->valuestring);
                if (name) resp.tool_calls[ti].name = strdup(name->valuestring);
                if (input) resp.tool_calls[ti].input_json = cJSON_PrintUnformatted(input);
                ti++;
            }
        }

        if (text_len > 0) {
            resp.text = calloc(1, (size_t)text_len + 1);
            cJSON_ArrayForEach(block, content) {
                cJSON *type = cJSON_GetObjectItem(block, "type");
                if (!type || strcmp(type->valuestring, "text")) continue;
                cJSON *t = cJSON_GetObjectItem(block, "text");
                if (t && t->valuestring) strcat(resp.text, t->valuestring);
            }
        }
    }

    cJSON_Delete(root);
    return resp;
}

ChatResponse provider_chat(HttpClient *http, const char *api_key,
                           const char *model, const char *system_prompt,
                           const char *messages_json, const char *tools_json,
                           float temperature) {
    char *body = build_request_body(model, system_prompt, messages_json,
                                    tools_json, temperature, false);

    char auth[300];
    snprintf(auth, sizeof(auth), "%s", api_key);

    const char *headers[] = {
        "x-api-key", auth,
        "anthropic-version", "2023-06-01",
    };

    HttpResponse hr = http_post_json(http, ANTHROPIC_URL, body, headers, 2);
    free(body);

    ChatResponse resp;
    if (hr.body) {
        resp = parse_response(hr.body);
    } else {
        memset(&resp, 0, sizeof(resp));
        resp.text = strdup("Error: no response from API");
    }

    http_response_free(&hr);
    return resp;
}

/* Streaming state */
typedef struct {
    StreamTextCb user_cb;
    void        *userdata;
    ChatResponse resp;
    /* Accumulate tool_use blocks across events */
    char *current_tool_id;
    char *current_tool_name;
    char *current_tool_input;
    size_t tool_input_len;
    size_t tool_input_cap;
} StreamState;

static bool stream_cb(const char *data, size_t len, void *ud) {
    StreamState *st = ud;
    (void)len;

    if (!strcmp(data, "[DONE]")) return false;

    cJSON *event = cJSON_Parse(data);
    if (!event) return true;

    cJSON *type = cJSON_GetObjectItem(event, "type");
    if (!type || !type->valuestring) { cJSON_Delete(event); return true; }

    if (!strcmp(type->valuestring, "content_block_delta")) {
        cJSON *delta = cJSON_GetObjectItem(event, "delta");
        if (delta) {
            cJSON *dt = cJSON_GetObjectItem(delta, "type");
            if (dt && !strcmp(dt->valuestring, "text_delta")) {
                cJSON *text = cJSON_GetObjectItem(delta, "text");
                if (text && text->valuestring) {
                    if (st->user_cb) {
                        if (!st->user_cb(text->valuestring, st->userdata)) {
                            cJSON_Delete(event);
                            return false;
                        }
                    }
                    /* Accumulate text */
                    if (!st->resp.text) st->resp.text = strdup(text->valuestring);
                    else {
                        size_t old = strlen(st->resp.text);
                        size_t add = strlen(text->valuestring);
                        st->resp.text = realloc(st->resp.text, old + add + 1);
                        memcpy(st->resp.text + old, text->valuestring, add + 1);
                    }
                }
            } else if (dt && !strcmp(dt->valuestring, "input_json_delta")) {
                cJSON *partial = cJSON_GetObjectItem(delta, "partial_json");
                if (partial && partial->valuestring) {
                    size_t plen = strlen(partial->valuestring);
                    if (st->tool_input_len + plen >= st->tool_input_cap) {
                        st->tool_input_cap = (st->tool_input_cap + plen) * 2;
                        st->current_tool_input = realloc(st->current_tool_input, st->tool_input_cap);
                    }
                    memcpy(st->current_tool_input + st->tool_input_len, partial->valuestring, plen);
                    st->tool_input_len += plen;
                    st->current_tool_input[st->tool_input_len] = '\0';
                }
            }
        }
    } else if (!strcmp(type->valuestring, "content_block_start")) {
        cJSON *cb_json = cJSON_GetObjectItem(event, "content_block");
        if (cb_json) {
            cJSON *cbt = cJSON_GetObjectItem(cb_json, "type");
            if (cbt && !strcmp(cbt->valuestring, "tool_use")) {
                cJSON *id = cJSON_GetObjectItem(cb_json, "id");
                cJSON *name = cJSON_GetObjectItem(cb_json, "name");
                free(st->current_tool_id);
                free(st->current_tool_name);
                st->current_tool_id = id ? strdup(id->valuestring) : NULL;
                st->current_tool_name = name ? strdup(name->valuestring) : NULL;
                st->tool_input_len = 0;
                if (!st->current_tool_input) {
                    st->tool_input_cap = 1024;
                    st->current_tool_input = malloc(st->tool_input_cap);
                }
                st->current_tool_input[0] = '\0';
            }
        }
    } else if (!strcmp(type->valuestring, "content_block_stop")) {
        /* Finalize tool call if active */
        if (st->current_tool_id) {
            int n = st->resp.num_tools;
            st->resp.tool_calls = realloc(st->resp.tool_calls, (size_t)(n + 1) * sizeof(ToolCall));
            st->resp.tool_calls[n].id = st->current_tool_id;
            st->resp.tool_calls[n].name = st->current_tool_name;
            st->resp.tool_calls[n].input_json = st->current_tool_input;
            st->resp.num_tools = n + 1;
            st->current_tool_id = NULL;
            st->current_tool_name = NULL;
            st->current_tool_input = NULL;
            st->tool_input_len = 0;
            st->tool_input_cap = 0;
        }
    } else if (!strcmp(type->valuestring, "message_delta")) {
        cJSON *delta = cJSON_GetObjectItem(event, "delta");
        if (delta) {
            cJSON *sr = cJSON_GetObjectItem(delta, "stop_reason");
            if (sr && sr->valuestring) {
                free(st->resp.stop_reason);
                st->resp.stop_reason = strdup(sr->valuestring);
            }
        }
        cJSON *usage = cJSON_GetObjectItem(event, "usage");
        if (usage) {
            cJSON *ot = cJSON_GetObjectItem(usage, "output_tokens");
            if (ot) st->resp.output_tokens = ot->valueint;
        }
    } else if (!strcmp(type->valuestring, "message_start")) {
        cJSON *msg = cJSON_GetObjectItem(event, "message");
        if (msg) {
            cJSON *usage = cJSON_GetObjectItem(msg, "usage");
            if (usage) {
                cJSON *it = cJSON_GetObjectItem(usage, "input_tokens");
                if (it) st->resp.input_tokens = it->valueint;
            }
        }
    }

    cJSON_Delete(event);
    return true;
}

ChatResponse provider_chat_stream(HttpClient *http, const char *api_key,
                                  const char *model, const char *system_prompt,
                                  const char *messages_json, const char *tools_json,
                                  float temperature,
                                  StreamTextCb cb, void *userdata) {
    char *body = build_request_body(model, system_prompt, messages_json,
                                    tools_json, temperature, true);

    const char *headers[] = {
        "x-api-key", api_key,
        "anthropic-version", "2023-06-01",
    };

    StreamState st = {0};
    st.user_cb = cb;
    st.userdata = userdata;

    http_post_stream(http, ANTHROPIC_URL, body, headers, 2, stream_cb, &st);
    free(body);

    return st.resp;
}

void chat_response_free(ChatResponse *r) {
    free(r->text);
    free(r->stop_reason);
    for (int i = 0; i < r->num_tools; i++) {
        free(r->tool_calls[i].id);
        free(r->tool_calls[i].name);
        free(r->tool_calls[i].input_json);
    }
    free(r->tool_calls);
    memset(r, 0, sizeof(*r));
}
