#ifndef CCLAW_PROVIDER_H
#define CCLAW_PROVIDER_H

#include "http.h"
#include "arena.h"
#include <stdbool.h>

/* Tool call from LLM response */
typedef struct {
    char *id;
    char *name;
    char *input_json;  /* Raw JSON string of arguments */
} ToolCall;

/* Chat response — either text or tool calls */
typedef struct {
    char  *text;          /* NULL if tool_use */
    ToolCall *tool_calls;
    int    num_tools;
    char  *stop_reason;   /* "end_turn", "tool_use", etc. */
    int    input_tokens;
    int    output_tokens;
} ChatResponse;

/* Streaming callback: text delta. Return false to abort. */
typedef bool (*StreamTextCb)(const char *delta, void *userdata);

/* Send a chat message (non-streaming). */
ChatResponse provider_chat(HttpClient *http,
                           const char *api_key,
                           const char *model,
                           const char *system_prompt,
                           const char *messages_json,
                           const char *tools_json,
                           float temperature);

/* Send a chat message with streaming text output. */
ChatResponse provider_chat_stream(HttpClient *http,
                                  const char *api_key,
                                  const char *model,
                                  const char *system_prompt,
                                  const char *messages_json,
                                  const char *tools_json,
                                  float temperature,
                                  StreamTextCb cb,
                                  void *userdata);

void chat_response_free(ChatResponse *r);

#endif
