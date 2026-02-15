# SeaClaw 🐾

**OpenClaw rewritten in C.** Single static binary. 709KB. <50ms cold start.

## Why

| | OpenClaw (Node.js) | CClaw (C) |
|---|---|---|
| Binary | ~80MB (with node) | **709KB** |
| Cold start | ~2-3s | **<50ms** |
| Memory (idle) | ~80-120MB | **~2-5MB** |
| Dependencies | ~800 npm packages | **3 C libs** |
| Docker image | ~250MB | **~3MB** |

## What it does

- Connects to Anthropic Claude API (streaming SSE)
- Reads workspace identity files (SOUL.md, AGENTS.md, USER.md, etc.)
- Tool execution: shell commands, file read/write
- Multi-turn conversations with tool loops
- Telegram bot (long-polling)
- Interactive CLI with streaming output
- Session persistence (JSON)
- Per-chat sessions for Telegram

## Build

```bash
# Get dependencies
cd deps
git clone --depth 1 https://github.com/DaveGamble/cJSON.git cjson
cp cjson/cJSON.c cjson/cJSON.h .
git clone --depth 1 --branch v2.28.8 https://github.com/Mbed-TLS/mbedtls.git
cd mbedtls && make lib && cd ../..

# Build
make
```

Or manually:
```bash
gcc -D_GNU_SOURCE -Wall -Wextra -std=c11 -O2 \
  -I deps -I deps/mbedtls/include -I src \
  src/*.c deps/cJSON.c \
  -L deps/mbedtls/library -lmbedtls -lmbedx509 -lmbedcrypto \
  -lpthread -lm -o cclaw
```

## Usage

```bash
# One-shot query
export ANTHROPIC_API_KEY=sk-ant-...
./cclaw "What is the capital of France?"

# Interactive CLI
./cclaw

# Telegram bot
export CCLAW_TELEGRAM_TOKEN=123456:ABC-...
./cclaw --telegram

# Custom workspace
./cclaw --workspace ~/my-agent "Read SOUL.md and introduce yourself"
```

## Config

Create `~/.cclaw/config`:
```ini
api_key = "sk-ant-..."
model = "claude-sonnet-4-20250514"
workspace = "/home/user/my-agent"
temperature = 0.7
telegram_enabled = true
telegram_token = "123456:ABC-..."
telegram_allowed = "12345678,87654321"
log_level = 2
```

Environment variables override config: `ANTHROPIC_API_KEY`, `CCLAW_API_KEY`, `CCLAW_MODEL`, `CCLAW_TELEGRAM_TOKEN`, `CCLAW_WORKSPACE`, `CCLAW_LOG_LEVEL`.

## Architecture

```
src/
├── main.c            Entry point, CLI, arg parsing
├── config.c          Config parser (key=value)
├── workspace.c       Read identity files, build system prompt
├── http.c            HTTP/1.1 + TLS (mbedtls), SSE streaming
├── provider.c        Anthropic Messages API (streaming + non-streaming)
├── provider_openai.c OpenAI Chat Completions API (streaming SSE)
├── tools.c           Tool registry + dispatch
├── tool_shell.c      Shell execution (fork+exec, piped stdout)
├── tool_file.c       File read/write with mkdir -p
├── session.c         Message history (cJSON array, file persistence)
├── memory.c          SQLite-backed memory with embedding search
├── telegram.c        Telegram Bot API (long-polling)
├── ws.c              WebSocket gateway (RFC 6455)
├── cron.c            Cron scheduler (5-field expressions)
├── arena.c           Bump allocator for per-request memory
└── log.c             Structured logging
```

Dependencies (vendored):
- **mbedtls** v2.28 — TLS 1.2/1.3
- **cJSON** — JSON parse/emit
- **SQLite** — Memory storage backend

## Features

### OpenAI Provider
Supports OpenAI Chat Completions API alongside Anthropic. Auto-detected when `OPENAI_API_KEY` is set. Streaming SSE, automatic translation of Anthropic-style tool definitions to OpenAI function calling format.

```bash
export OPENAI_API_KEY=sk-...
./cclaw "Hello from OpenAI"
```

Or set in config:
```ini
provider = "openai"
openai_api_key = "sk-..."
openai_model = "gpt-4o"
```

### Memory Search (SQLite + Embeddings)
Persistent key-value memory backed by SQLite. Stores float embedding vectors as BLOBs for semantic search via cosine similarity with top-k selection.

- `memory_store(key, value, embedding)` — Save with optional embedding vector
- `memory_get(key)` — Retrieve by key
- `memory_delete(key)` — Remove entry
- `memory_search(query_embedding, top_k)` — Semantic search across all stored embeddings

### WebSocket Gateway
RFC 6455 compliant WebSocket server for real-time bidirectional communication. Poll-based multiplexing supporting up to 64 concurrent clients.

```ini
ws_enabled = true
ws_port = 8080
ws_auth_token = "optional-bearer-token"
```

Features: masked frames, ping/pong keepalive, close handshake, optional auth via Bearer token or `?token=` query param.

### Cron Scheduler
Built-in cron with standard 5-field expressions (`min hour dom month dow`). Supports wildcards (`*`) and step values (`*/N`). Runs in a background thread, checks every 30 seconds.

```ini
cron_enabled = true
```

## What's not here (yet)

- [ ] Discord/Slack/WhatsApp channels
- [ ] Browser tool
- [ ] Static musl build
- [ ] Cross-compilation

## License

Apache-2.0 (same as OpenClaw)
