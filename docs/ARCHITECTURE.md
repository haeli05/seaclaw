# Architecture Overview

CClaw (SeaClaw) is a minimal AI assistant framework written in C — a from-scratch rewrite of OpenClaw (Node.js) as a single static binary.

## Design Goals

- **Tiny binary**: ~709KB static binary vs ~80MB Node.js
- **Fast startup**: <50ms cold start vs 2-3s
- **Low memory**: ~2-5MB idle vs ~80-120MB
- **Minimal dependencies**: 3 vendored C libraries (mbedtls, cJSON, SQLite3)
- **Single-threaded core** with background threads for cron and WebSocket gateway

## Module Dependency Graph

```
                        ┌──────────┐
                        │  main.c  │
                        └────┬─────┘
           ┌─────────────────┼─────────────────────┐
           │                 │                      │
    ┌──────▼──────┐   ┌─────▼──────┐       ┌──────▼───────┐
    │  config.c   │   │ workspace.c│       │  telegram.c  │
    └─────────────┘   └─────┬──────┘       └──────┬───────┘
                            │                      │
                       ┌────▼────┐           ┌─────▼─────┐
                       │ arena.c │           │  http.c    │
                       └─────────┘           └─────┬─────┘
                                                   │
                                    ┌──────────────┼──────────────┐
                                    │              │              │
                             ┌──────▼──────┐ ┌────▼─────┐ ┌─────▼──────┐
                             │ provider.c  │ │provider_  │ │  ws.c      │
                             │ (Anthropic) │ │openai.c   │ │(WebSocket) │
                             └─────────────┘ └──────────┘ └────────────┘

    ┌──────────┐    ┌──────────────┐    ┌──────────────┐
    │ tools.c  │───►│ tool_shell.c │    │ tool_file.c  │
    │(dispatch)│    └──────────────┘    └──────────────┘
    └──────────┘

    ┌──────────┐    ┌──────────┐    ┌──────────┐
    │session.c │    │ memory.c │    │  cron.c  │
    └──────────┘    └──────────┘    └──────────┘

    ┌──────────┐
    │  log.c   │  (used by all modules)
    └──────────┘
```

## Data Flow

### 1. Startup

```
main() → config_defaults() → config_load() → config_load_env()
       → ws_build_system_prompt()  (reads SOUL.md, AGENTS.md, etc.)
       → tools_get_definitions()   (JSON tool schemas)
       → http_client_new()         (initialize mbedtls TLS context)
       → cron_run() in thread      (background scheduler)
       → ws_server_start() in thread (if gateway_port > 0)
```

### 2. Message Processing (Agent Turn)

```
User input (CLI/Telegram/WebSocket)
  │
  ▼
agent_turn()
  ├── session_add_user()          — append to message history
  ├── session_messages_json()     — serialize to JSON
  ├── provider_chat[_stream]()    — call LLM API
  │     ├── build_request_body()  — construct Anthropic/OpenAI request
  │     ├── http_post_json() or http_post_stream()
  │     └── parse_response()      — extract text + tool calls
  │
  ├── If tool_calls:
  │     ├── tool_execute()        — dispatch to tool handler
  │     │     ├── tool_shell_exec()  — fork/exec shell command
  │     │     ├── tool_file_read()   — read file contents
  │     │     └── tool_file_write()  — write file (with mkdir -p)
  │     ├── session_add_tool_use()   — record in history
  │     ├── session_add_tool_result() — record tool output
  │     └── Loop back to provider_chat() (up to 10 turns)
  │
  ├── session_add_assistant()     — record final response
  └── session_save()              — persist to disk
```

### 3. Channel Input Sources

| Channel | Entry Point | Session ID | Streaming |
|---------|------------|------------|-----------|
| CLI | `cli_mode()` → `fgets()` | `"cli"` | Yes |
| Telegram | `telegram_poll_loop()` → callback | `"tg_{chat_id}"` | No |
| WebSocket | `ws_server_start()` → `ws_on_message()` | `"ws_{fd}"` | No |
| One-shot | `argv[1]` from command line | `NULL` (ephemeral) | Yes |

## Threading Model

```
Main Thread          Cron Thread          WebSocket Thread
    │                     │                     │
    │  CLI/Telegram       │  cron_run()         │  ws_server_start()
    │  message loop       │  sleep(30) loop     │  poll() event loop
    │                     │  fires callbacks    │  accepts connections
    │                     │                     │  reads frames
    │                     │                     │  calls agent_turn()
```

- **Main thread**: Runs the primary channel (CLI interactive mode or Telegram long-polling)
- **Cron thread**: Background scheduler, checks every 30 seconds for due jobs
- **WebSocket thread**: `poll()`-based event loop accepting gateway connections

## Memory Management

CClaw uses three allocation strategies:

1. **Arena allocator** (`arena.c`): Bump allocator for per-request data (system prompt, workspace file reads). Auto-grows by doubling. Reset/free at end of lifetime.

2. **malloc/free**: Standard heap for long-lived objects (sessions, HTTP responses, tool results). Each module documents ownership — caller frees unless noted.

3. **cJSON**: JSON objects managed through cJSON's own allocator. Always `cJSON_Delete()` when done.

## TLS / HTTP

All HTTP is HTTPS via mbedtls (vendored v2.28):
- System CA certs loaded from `/etc/ssl/certs`
- One `HttpClient` holds the TLS config, shared across requests
- Each request opens a new TCP+TLS connection (no keepalive/pooling)
- SSE streaming: reads line-by-line, dispatches `data:` lines to callback

## File Layout

```
seaclaw/
├── src/                 All source code
│   ├── main.c           Entry point, CLI, Telegram, arg parsing
│   ├── config.{c,h}     Configuration loading
│   ├── workspace.{c,h}  System prompt construction
│   ├── http.{c,h}       HTTP/TLS client (mbedtls)
│   ├── provider.{c,h}   Anthropic Claude API
│   ├── provider_openai.{c,h}  OpenAI API
│   ├── tools.{c,h}      Tool registry and dispatch
│   ├── tool_shell.{c,h}  Shell command execution
│   ├── tool_file.{c,h}   File read/write tools
│   ├── session.{c,h}     Conversation history
│   ├── telegram.{c,h}    Telegram Bot API
│   ├── memory.{c,h}      SQLite memory with embeddings
│   ├── ws.{c,h}          WebSocket server (RFC 6455)
│   ├── cron.{c,h}        Cron scheduler
│   ├── arena.{c,h}       Bump allocator
│   └── log.{c,h}         Structured logging
├── deps/                Vendored dependencies
│   ├── cjson/           cJSON library
│   └── mbedtls/         mbedTLS library
├── Makefile             Build system
└── README.md
```
