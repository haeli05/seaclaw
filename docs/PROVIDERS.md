# LLM Providers

CClaw supports multiple LLM providers through a common `ChatResponse` interface. Provider selection is automatic based on the `provider` config field.

## Supported Providers

### Anthropic (default)

**File:** `src/provider.c`  
**API:** [Messages API](https://docs.anthropic.com/claude/reference/messages) (`https://api.anthropic.com/v1/messages`)  
**API Version:** `2023-06-01`

```ini
provider = "anthropic"
api_key = "sk-ant-api03-..."
model = "claude-sonnet-4-20250514"
```

Or via environment:
```bash
export ANTHROPIC_API_KEY=sk-ant-api03-...
```

**Features:**
- Non-streaming and streaming (SSE) modes
- Tool use with `input_schema` format
- Content blocks: `text` and `tool_use`
- Streaming accumulates `input_json_delta` for tool arguments

**Request format:**
```json
{
  "model": "claude-sonnet-4-20250514",
  "max_tokens": 8192,
  "temperature": 0.7,
  "system": "...",
  "messages": [...],
  "tools": [...]
}
```

**Headers:**
- `x-api-key: <api_key>`
- `anthropic-version: 2023-06-01`

### OpenAI

**File:** `src/provider_openai.c`  
**API:** [Chat Completions](https://platform.openai.com/docs/api-reference/chat) (`https://api.openai.com/v1/chat/completions`)

```ini
provider = "openai"
api_key = "sk-..."
model = "gpt-4o"
```

Or via environment:
```bash
export OPENAI_API_KEY=sk-...
# Provider auto-detected as "openai" when OPENAI_API_KEY is set
```

**Features:**
- Non-streaming and streaming (SSE) modes
- Function calling (auto-converted from Anthropic tool format)
- Streaming tool call assembly with incremental argument chunks

**Tool format conversion** (automatic):
```
Anthropic format:                    OpenAI format:
{ "name": "shell",        →         { "type": "function",
  "description": "...",               "function": {
  "input_schema": {...} }               "name": "shell",
                                         "description": "...",
                                         "parameters": {...} } }
```

**Stop reason mapping:**
| OpenAI | Mapped to (Anthropic-style) |
|--------|---------------------------|
| `"stop"` | `"end_turn"` |
| `"tool_calls"` | `"tool_use"` |
| other | passed through |

## Provider Selection Logic

In `main.c`, the provider is selected per-turn:

```c
bool is_openai = !strcmp(ctx->cfg->provider, "openai");

if (is_openai) {
    resp = openai_chat[_stream](...);
} else {
    resp = provider_chat[_stream](...);  // Anthropic
}
```

Auto-detection in `config_load_env()`:
- If `OPENAI_API_KEY` is set and no other key is configured, provider switches to `"openai"` automatically

## Common Response Format

Both providers return the same `ChatResponse` struct:

```c
typedef struct {
    char  *text;          // Final text (NULL if tool_use only)
    ToolCall *tool_calls; // Array of tool calls
    int    num_tools;     // Number of tool calls
    char  *stop_reason;   // "end_turn" or "tool_use"
    int    input_tokens;
    int    output_tokens;
} ChatResponse;

typedef struct {
    char *id;             // Tool call ID
    char *name;           // Tool name (e.g., "shell")
    char *input_json;     // Arguments as JSON string
} ToolCall;
```

## Adding a New Provider

### Step 1: Create provider files

```c
// src/provider_myprovider.h
#ifndef CCLAW_PROVIDER_MYPROVIDER_H
#define CCLAW_PROVIDER_MYPROVIDER_H

#include "provider.h"
#include "http.h"

ChatResponse myprovider_chat(HttpClient *http,
                              const char *api_key,
                              const char *model,
                              const char *system_prompt,
                              const char *messages_json,
                              const char *tools_json,
                              float temperature);

ChatResponse myprovider_chat_stream(HttpClient *http,
                                     const char *api_key,
                                     const char *model,
                                     const char *system_prompt,
                                     const char *messages_json,
                                     const char *tools_json,
                                     float temperature,
                                     StreamTextCb cb,
                                     void *userdata);
#endif
```

### Step 2: Implement the provider

Key responsibilities:
1. Convert `tools_json` (Anthropic format) to your API's tool format
2. Build the request body JSON
3. Parse the response into `ChatResponse`
4. For streaming: handle SSE events, accumulate text and tool calls
5. Map stop/finish reasons to `"end_turn"` or `"tool_use"`

### Step 3: Wire into main.c

Add a new branch in `agent_turn()`:

```c
if (!strcmp(ctx->cfg->provider, "myprovider")) {
    resp = myprovider_chat(ctx->http, ...);
}
```

### Step 4: Update config

Add env var handling in `config_load_env()` for auto-detection.

### Step 5: Add to Makefile

```makefile
SRCS = ... src/provider_myprovider.c
```
