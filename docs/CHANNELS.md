# Channels

CClaw supports multiple input channels for receiving messages and sending responses. Each channel feeds into the same `agent_turn()` core.

## Available Channels

### 1. Interactive CLI

**Entry:** `cli_mode()` in `main.c`  
**Session ID:** `"cli"`  
**Streaming:** Yes

The default mode when no arguments or flags are passed. Reads from stdin with a colored prompt, streams responses to stdout.

```bash
./cclaw
# CClaw v0.1.0 — type /quit to exit
# you> Hello
# cclaw> Hi there!
```

**Commands:** `/quit`, `/exit`

### 2. One-Shot CLI

**Entry:** `main()` directly  
**Session ID:** `NULL` (ephemeral, not saved)  
**Streaming:** Yes

Pass a prompt as a positional argument:

```bash
./cclaw "What is 2+2?"
```

### 3. Telegram Bot

**Entry:** `telegram_poll_loop()` in `telegram.c`  
**Session ID:** `"tg_{chat_id}"`  
**Streaming:** No (sends complete response)

Uses Telegram Bot API long-polling (30-second timeout). Each chat gets its own persistent session.

```bash
export CCLAW_TELEGRAM_TOKEN=123456:ABC...
./cclaw --telegram
```

**Features:**
- User allowlist (by ID or username)
- Typing indicator while processing
- Markdown-formatted replies
- Per-chat conversation history

**Key functions:**
```c
// Start polling (blocking)
int telegram_poll_loop(HttpClient *http, const CClawConfig *cfg,
                       TelegramMsgHandler handler, void *userdata);

// Send a message
int telegram_send(HttpClient *http, const char *token,
                  long long chat_id, const char *text);

// Show typing indicator
int telegram_send_typing(HttpClient *http, const char *token,
                         long long chat_id);
```

### 4. WebSocket Gateway

**Entry:** `ws_server_start()` in `ws.c`, started as a background thread  
**Session ID:** `"ws_{client_fd}"`  
**Streaming:** No (sends complete response as single WS text frame)

Always runs in the background when `gateway_port > 0` (default: 3578).

```bash
./cclaw --gateway-port 8080
```

**Protocol:** Standard WebSocket (RFC 6455) over plain TCP.

**Authentication:** Optional via `gateway_token` config:
- `Authorization: Bearer <token>` header during handshake
- `?token=<token>` query parameter in the upgrade URL

**Client example:**
```javascript
const ws = new WebSocket('ws://localhost:3578?token=my-secret');
ws.onmessage = (e) => console.log(e.data);
ws.send('Hello from WebSocket!');
```

## Adding a New Channel

To add a new channel (e.g., Discord, Slack, HTTP API):

### Step 1: Create the channel files

```c
// src/mychannel.h
#ifndef CCLAW_MYCHANNEL_H
#define CCLAW_MYCHANNEL_H

#include "http.h"
#include "config.h"

typedef char *(*MyChannelMsgHandler)(const char *msg, void *userdata);

int mychannel_start(HttpClient *http, const CClawConfig *cfg,
                    MyChannelMsgHandler handler, void *userdata);
#endif
```

```c
// src/mychannel.c
#include "mychannel.h"
#include "log.h"

int mychannel_start(HttpClient *http, const CClawConfig *cfg,
                    MyChannelMsgHandler handler, void *userdata) {
    // Your channel's event loop or polling
    // For each incoming message:
    //   char *reply = handler(message_text, userdata);
    //   send_reply(reply);
    //   free(reply);
    return 0;
}
```

### Step 2: Add config fields

In `config.h`, add fields for your channel's settings:
```c
typedef struct {
    // ... existing fields ...
    bool mychannel_enabled;
    char mychannel_token[256];
} CClawConfig;
```

Add parsing in `config.c`'s `config_load()` and `config_load_env()`.

### Step 3: Wire into main.c

```c
#include "mychannel.h"

// In main():
if (mychannel_mode) {
    // Create handler that wraps agent_turn()
    mychannel_start(http, &cfg, mychannel_handler, &ctx);
}
```

### Step 4: Update the Makefile

Add your `.c` file to `SRCS`:
```makefile
SRCS = ... src/mychannel.c
```

### Key Pattern

All channels follow the same pattern:
1. Receive a text message from the user
2. Create/load a `Session` with a channel-specific ID
3. Call `agent_turn(ctx, session, message, streaming)` 
4. Send the returned text as a reply
5. Free the session and reply string

The `AgentCtx` struct (defined in `main.c`) holds everything needed:
```c
typedef struct {
    CClawConfig *cfg;
    HttpClient  *http;
    char        *system_prompt;
    char        *tools_json;
} AgentCtx;
```
