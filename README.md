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
├── main.c        Entry point, CLI, arg parsing
├── config.c      Config parser (key=value)
├── workspace.c   Read identity files, build system prompt
├── http.c        HTTP/1.1 + TLS (mbedtls), SSE streaming
├── provider.c    Anthropic Messages API (streaming + non-streaming)
├── tools.c       Tool registry + dispatch
├── tool_shell.c  Shell execution (fork+exec, piped stdout)
├── tool_file.c   File read/write with mkdir -p
├── session.c     Message history (cJSON array, file persistence)
├── telegram.c    Telegram Bot API (long-polling)
├── arena.c       Bump allocator for per-request memory
└── log.c         Structured logging
```

Dependencies (vendored):
- **mbedtls** v2.28 — TLS 1.2/1.3
- **cJSON** — JSON parse/emit

## What's not here (yet)

- [ ] OpenAI provider
- [ ] Memory search (SQLite + embeddings)
- [ ] WebSocket gateway
- [ ] Cron scheduler
- [ ] Discord/Slack/WhatsApp channels
- [ ] Browser tool
- [ ] Static musl build
- [ ] Cross-compilation

## License

Apache-2.0 (same as OpenClaw)
