#ifndef CCLAW_PROVIDER_OPENAI_H
#define CCLAW_PROVIDER_OPENAI_H

#include "provider.h"
#include "http.h"

/* OpenAI Chat Completions API (non-streaming). */
ChatResponse openai_chat(HttpClient *http,
                         const char *api_key,
                         const char *model,
                         const char *system_prompt,
                         const char *messages_json,
                         const char *tools_json,
                         float temperature);

/* OpenAI Chat Completions API (streaming). */
ChatResponse openai_chat_stream(HttpClient *http,
                                const char *api_key,
                                const char *model,
                                const char *system_prompt,
                                const char *messages_json,
                                const char *tools_json,
                                float temperature,
                                StreamTextCb cb,
                                void *userdata);

#endif
