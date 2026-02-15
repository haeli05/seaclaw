/*
 * OpenAI Chat Completions provider.
 *
 * Supports both non-streaming and streaming (SSE) modes.
 * Translates between Anthropic-style tool definitions and OpenAI function calling format.
 */

#include "provider_openai.h"
#include "log.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define OPENAI_URL "https://api.openai.com/v1/chat/completions"
#define MAX_TOKENS 8192

/*
 * Convert Anthropic-style tools JSON to OpenAI function calling format.
 * Anthropic: [{ name, description, input_schema }]
 * OpenAI:    [{ type: "function", function: { name, description, parameters } }]
 */
static cJSON *convert_tools(const char *tools_json) {
    if (!tools_json || !tools_json[0]) return NULL;

    cJSON *src = cJSON_Parse(tools_json);
    if (!src || !cJSON_IsArray(src)) { cJSON_Delete(src); return NULL; }

    cJSON *out = cJSON_CreateArray();
    cJSON *tool;
    cJSON_ArrayForEach(tool, src) {
        cJSON *name = cJSON_GetObjectItem(tool, "name");
        cJSON *desc = cJSON_GetObjectItem(tool, "description");
        cJSON *schema = cJSON_GetObjectItem(tool, "input_schema");

        cJSON *fn = cJSON_CreateObject();
        if (name)   cJSON_AddStringToObject(fn, "name", name->valuestring);
        if (desc)   cJSON_AddStringToObject(fn, "description", desc->valuestring);
        if (schema) cJSON_AddItemToObject(fn, "parameters", cJSON_Duplicate(schema, 1));

        cJSON *wrapper = cJSON_CreateObject();
        cJSON_AddStringToObject(wrapper, "type", "function");
        cJSON_AddItemToObject(wrapper, "function", fn);
        cJSON_AddItemToArray(out, wrapper);
    }

    cJSON_Delete(src);
    return out;
}

/*
 * Build OpenAI request body.
 * Messages are in Anthropic format (role/content), which is mostly compatible.
 * We prepend a system message if provided.
 */
static char *build_request_body(const char *model, const char *system_prompt,
                                const char *messages_json, const char *tools_json,
                                float temperature, bool stream) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddNumberToObject(root, "max_tokens", MAX_TOKENS);
    cJSON_AddNumberToObject(root, "temperature", (double)temperature);
    if (stream) cJSON_AddBoolToObject(root, "stream", 1);

    /* Build messages array with system prompt prepended */
    cJSON *msgs = cJSON_CreateArray();

    if (system_prompt && system_prompt[0]) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(msgs, sys);
    }

    cJSON *user_msgs = cJSON_Parse(messages_json);
    if (user_msgs && cJSON_IsArray(user_msgs)) {
        cJSON *m;
        cJSON_ArrayForEach(m, user_msgs) {
            cJSON_AddItemToArray(msgs, cJSON_Duplicate(m, 1));
        }
        cJSON_Delete(user_msgs);
    } else {
        /* Fallback: single user message */
        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", "user");
        cJSON_AddStringToObject(m, "content", messages_json);
        cJSON_AddItemToArray(msgs, m);
        cJSON_Delete(user_msgs);
    }

    cJSON_AddItemToObject(root, "messages", msgs);

    /* Convert and add tools */
    cJSON *tools = convert_tools(tools_json);
    if (tools && cJSON_GetArraySize(tools) > 0) {
        cJSON_AddItemToObject(root, "tools", tools);
    } else {
        cJSON_Delete(tools);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

/* Parse OpenAI non-streaming response. */
static ChatResponse parse_response(const char *json_str) {
    ChatResponse resp = {0};
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERROR("Failed to parse OpenAI response");
        resp.text = strdup("Error: failed to parse OpenAI API response");
        return resp;
    }

    /* Check for error */
    cJSON *err = cJSON_GetObjectItem(root, "error");
    if (err) {
        cJSON *msg = cJSON_GetObjectItem(err, "message");
        resp.text = strdup(msg ? msg->valuestring : "Unknown OpenAI API error");
        cJSON_Delete(root);
        return resp;
    }

    /* Usage */
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
        if (pt) resp.input_tokens = pt->valueint;
        if (ct) resp.output_tokens = ct->valueint;
    }

    /* Choices */
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        resp.text = strdup("Error: no choices in response");
        cJSON_Delete(root);
        return resp;
    }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *finish = cJSON_GetObjectItem(choice, "finish_reason");
    if (finish && finish->valuestring) {
        /* Map OpenAI finish reasons to Anthropic-style */
        if (!strcmp(finish->valuestring, "stop"))
            resp.stop_reason = strdup("end_turn");
        else if (!strcmp(finish->valuestring, "tool_calls"))
            resp.stop_reason = strdup("tool_use");
        else
            resp.stop_reason = strdup(finish->valuestring);
    }

    cJSON *message = cJSON_GetObjectItem(choice, "message");
    if (!message) { cJSON_Delete(root); return resp; }

    /* Text content */
    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (content && content->valuestring && content->valuestring[0]) {
        resp.text = strdup(content->valuestring);
    }

    /* Tool calls */
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        int n = cJSON_GetArraySize(tool_calls);
        resp.tool_calls = calloc((size_t)n, sizeof(ToolCall));
        resp.num_tools = n;
        for (int i = 0; i < n; i++) {
            cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
            cJSON *id = cJSON_GetObjectItem(tc, "id");
            cJSON *fn = cJSON_GetObjectItem(tc, "function");
            if (id) resp.tool_calls[i].id = strdup(id->valuestring);
            if (fn) {
                cJSON *name = cJSON_GetObjectItem(fn, "name");
                cJSON *args = cJSON_GetObjectItem(fn, "arguments");
                if (name) resp.tool_calls[i].name = strdup(name->valuestring);
                if (args) resp.tool_calls[i].input_json = strdup(args->valuestring);
            }
        }
    }

    cJSON_Delete(root);
    return resp;
}

ChatResponse openai_chat(HttpClient *http, const char *api_key,
                          const char *model, const char *system_prompt,
                          const char *messages_json, const char *tools_json,
                          float temperature) {
    char *body = build_request_body(model, system_prompt, messages_json,
                                    tools_json, temperature, false);

    char auth[300];
    snprintf(auth, sizeof(auth), "Bearer %s", api_key);

    const char *headers[] = {
        "Authorization", auth,
    };

    HttpResponse hr = http_post_json(http, OPENAI_URL, body, headers, 1);
    free(body);

    ChatResponse resp;
    if (hr.body) {
        resp = parse_response(hr.body);
    } else {
        memset(&resp, 0, sizeof(resp));
        resp.text = strdup("Error: no response from OpenAI API");
    }

    http_response_free(&hr);
    return resp;
}

/* Streaming state */
typedef struct {
    StreamTextCb user_cb;
    void        *userdata;
    ChatResponse resp;
    /* Tool call accumulation (OpenAI streams tool calls incrementally) */
    int   current_tool_idx;
    char *tool_ids[32];
    char *tool_names[32];
    char *tool_args[32];
    size_t tool_args_len[32];
    size_t tool_args_cap[32];
    int    num_tools;
} OaiStreamState;

static bool oai_stream_cb(const char *data, size_t len, void *ud) {
    OaiStreamState *st = ud;
    (void)len;

    if (!strcmp(data, "[DONE]")) return false;

    cJSON *event = cJSON_Parse(data);
    if (!event) return true;

    cJSON *choices = cJSON_GetObjectItem(event, "choices");
    if (!choices || cJSON_GetArraySize(choices) == 0) { cJSON_Delete(event); return true; }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *delta = cJSON_GetObjectItem(choice, "delta");
    cJSON *finish = cJSON_GetObjectItem(choice, "finish_reason");

    if (finish && finish->valuestring) {
        if (!strcmp(finish->valuestring, "stop"))
            st->resp.stop_reason = strdup("end_turn");
        else if (!strcmp(finish->valuestring, "tool_calls"))
            st->resp.stop_reason = strdup("tool_use");
        else
            st->resp.stop_reason = strdup(finish->valuestring);
    }

    if (delta) {
        /* Text content delta */
        cJSON *content = cJSON_GetObjectItem(delta, "content");
        if (content && content->valuestring) {
            if (st->user_cb) {
                if (!st->user_cb(content->valuestring, st->userdata)) {
                    cJSON_Delete(event);
                    return false;
                }
            }
            if (!st->resp.text) {
                st->resp.text = strdup(content->valuestring);
            } else {
                size_t old = strlen(st->resp.text);
                size_t add = strlen(content->valuestring);
                st->resp.text = realloc(st->resp.text, old + add + 1);
                memcpy(st->resp.text + old, content->valuestring, add + 1);
            }
        }

        /* Tool calls delta */
        cJSON *tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
        if (tool_calls && cJSON_IsArray(tool_calls)) {
            cJSON *tc;
            cJSON_ArrayForEach(tc, tool_calls) {
                cJSON *idx_j = cJSON_GetObjectItem(tc, "index");
                int idx = idx_j ? idx_j->valueint : 0;
                if (idx >= 32) continue;

                if (idx >= st->num_tools) st->num_tools = idx + 1;

                cJSON *id = cJSON_GetObjectItem(tc, "id");
                if (id && id->valuestring) {
                    free(st->tool_ids[idx]);
                    st->tool_ids[idx] = strdup(id->valuestring);
                }

                cJSON *fn = cJSON_GetObjectItem(tc, "function");
                if (fn) {
                    cJSON *name = cJSON_GetObjectItem(fn, "name");
                    if (name && name->valuestring) {
                        free(st->tool_names[idx]);
                        st->tool_names[idx] = strdup(name->valuestring);
                    }
                    cJSON *args = cJSON_GetObjectItem(fn, "arguments");
                    if (args && args->valuestring) {
                        size_t alen = strlen(args->valuestring);
                        if (st->tool_args_len[idx] + alen >= st->tool_args_cap[idx]) {
                            st->tool_args_cap[idx] = (st->tool_args_cap[idx] + alen) * 2 + 64;
                            st->tool_args[idx] = realloc(st->tool_args[idx], st->tool_args_cap[idx]);
                        }
                        memcpy(st->tool_args[idx] + st->tool_args_len[idx], args->valuestring, alen);
                        st->tool_args_len[idx] += alen;
                        st->tool_args[idx][st->tool_args_len[idx]] = '\0';
                    }
                }
            }
        }
    }

    /* Usage (sometimes in stream chunks) */
    cJSON *usage = cJSON_GetObjectItem(event, "usage");
    if (usage) {
        cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
        if (pt) st->resp.input_tokens = pt->valueint;
        if (ct) st->resp.output_tokens = ct->valueint;
    }

    cJSON_Delete(event);
    return true;
}

ChatResponse openai_chat_stream(HttpClient *http, const char *api_key,
                                const char *model, const char *system_prompt,
                                const char *messages_json, const char *tools_json,
                                float temperature,
                                StreamTextCb cb, void *userdata) {
    char *body = build_request_body(model, system_prompt, messages_json,
                                    tools_json, temperature, true);

    char auth[300];
    snprintf(auth, sizeof(auth), "Bearer %s", api_key);

    const char *headers[] = {
        "Authorization", auth,
    };

    OaiStreamState st = {0};
    st.user_cb = cb;
    st.userdata = userdata;

    http_post_stream(http, OPENAI_URL, body, headers, 1, oai_stream_cb, &st);
    free(body);

    /* Finalize tool calls */
    if (st.num_tools > 0) {
        st.resp.tool_calls = calloc((size_t)st.num_tools, sizeof(ToolCall));
        st.resp.num_tools = st.num_tools;
        for (int i = 0; i < st.num_tools; i++) {
            st.resp.tool_calls[i].id = st.tool_ids[i];
            st.resp.tool_calls[i].name = st.tool_names[i];
            st.resp.tool_calls[i].input_json = st.tool_args[i];
        }
    }

    return st.resp;
}
