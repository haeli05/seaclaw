# CClaw — OpenClaw Rewritten in C

## Reality Check

OpenClaw is ~190MB of TypeScript source, ~490k lines compiled JS, 311 modules. It handles:
- WebSocket gateway server
- Multi-channel messaging (Telegram, Discord, Slack, WhatsApp, iMessage, Signal, etc.)
- LLM provider abstraction (Anthropic, OpenAI, OpenRouter, etc.)
- Session management with persistence
- Tool execution (shell, file I/O, browser, memory, cron, etc.)
- Streaming token responses
- Config management (TOML/JSON)
- Node pairing & device auth (Ed25519)
- Cron scheduler
- Memory/embedding search
- TTS integration
- Skill loading system
- Plugin SDK

Rewriting ALL of that in C is 6-12 months for a team. But a functional core that does what 90% of users need? That's achievable.

## Architecture: What CClaw Actually Is

A single static binary (~500KB-2MB) that:
1. Connects to an LLM API (Anthropic/OpenAI)
2. Reads workspace files (SOUL.md, AGENTS.md, etc.)
3. Listens on Telegram (webhook or long-polling)
4. Executes tools (shell, file read/write, memory)
5. Streams responses back
6. Persists sessions to disk

No Node.js. No V8. No npm. Just C, a TLS library, and raw sockets.

## Dependencies (minimal, vendored or statically linked)

| Lib | Purpose | Size |
|-----|---------|------|
| bearssl or mbedtls | TLS 1.3 | ~200KB |
| cJSON | JSON parse/emit | ~30KB |
| libuv (optional) | Event loop | ~150KB |
| sqlite3 | Session/memory storage | ~600KB |

Or go full zero-dep: raw syscalls + hand-rolled TLS (insane but possible).

**Recommended:** mbedtls + cJSON + sqlite3 = ~830KB statically linked.

## Build Plan — 7 Phases

### Phase 1: Skeleton + Config (Day 1)
```
cclaw/
├── Makefile
├── src/
│   ├── main.c          # Entry point, arg parsing
│   ├── config.c/h      # TOML-ish config parser
│   ├── workspace.c/h   # Read SOUL.md, AGENTS.md, etc.
│   └── log.c/h         # Minimal structured logging
├── deps/
│   ├── cjson/          # Vendored
│   └── mbedtls/        # Vendored
└── tests/
    └── test_config.c
```

Deliverable: `./cclaw --workspace ~/clawd` reads config, prints system prompt.

### Phase 2: HTTP Client + LLM Provider (Day 2-3)
```
src/
├── http.c/h            # Raw HTTP/1.1 over TLS
├── stream.c/h          # SSE (Server-Sent Events) parser
├── provider.c/h        # Anthropic Messages API
└── provider_openai.c/h # OpenAI Chat Completions API
```

- Persistent TLS connections (connection pooling)
- Streaming token-by-token via SSE
- Tool use response parsing (Anthropic format)
- Retry with exponential backoff

Deliverable: `./cclaw "What is 2+2?"` → streams Anthropic response to stdout.

### Phase 3: Tool Execution (Day 4-5)
```
src/
├── tools.c/h           # Tool registry + dispatch
├── tool_shell.c/h      # Execute shell commands (fork+exec)
├── tool_file.c/h       # Read/write/edit files
├── tool_memory.c/h     # SQLite-backed memory store + search
└── tool_web.c/h        # HTTP GET + HTML-to-text extraction
```

- Tool call parsing from LLM response
- Result formatting back to LLM
- Multi-turn tool loops
- Timeout + resource limits on shell exec

Deliverable: Agent can use tools in a conversation loop.

### Phase 4: Session Management (Day 6)
```
src/
├── session.c/h         # Session create/load/save
├── history.c/h         # Message history (SQLite)
└── context.c/h         # Context window management + compaction
```

- SQLite-backed persistent sessions
- Token counting (tiktoken-compatible)
- Context window compaction (summarize when full)
- Multi-session support

### Phase 5: Telegram Channel (Day 7-8)
```
src/
├── telegram.c/h        # Bot API (long-polling, not webhook)
├── channel.c/h         # Channel abstraction
└── gateway.c/h         # Message routing
```

- Long-polling (no webhook server needed = simpler)
- Send/receive text messages
- User allowlist
- Markdown formatting
- Inline buttons (optional)

Deliverable: Full working Telegram bot with tools.

### Phase 6: WebSocket Gateway (Day 9-10)
```
src/
├── ws.c/h              # WebSocket server (RFC 6455)
├── gateway_ws.c/h      # OpenClaw-compatible gateway protocol
└── cron.c/h            # Cron scheduler (timer-based)
```

- Minimal WebSocket server for CLI/webchat clients
- Heartbeat system
- Cron job execution

### Phase 7: Polish + Release (Day 11-12)
- Static linking (`musl` on Linux, native on macOS)
- Cross-compilation targets (x86_64, aarch64, armv7)
- `./cclaw onboard` interactive setup
- Man page
- Dockerfile (FROM scratch — literally empty base image)
- Benchmarks vs OpenClaw (startup time, memory, throughput)

## Target Metrics

| Metric | OpenClaw (Node.js) | CClaw (C) |
|--------|-------------------|-----------|
| Binary size | ~80MB (with node) | ~1-2MB |
| Cold start | ~2-3s | <50ms |
| Memory idle | ~80-120MB | ~2-5MB |
| Memory active | ~200-400MB | ~10-30MB |
| Dependencies | ~800 npm packages | 3 C libs |
| Docker image | ~250MB | ~3MB |

## What CClaw Does NOT Do (v1)

- No plugin SDK (compile channels in, not loadable)
- No browser automation (needs headless Chrome)
- No TTS (just call an API)
- No node pairing (SSH instead)
- No webchat UI (gateway only)
- No Signal/iMessage/Discord (Telegram + CLI only in v1)

## File Count Estimate

~25-30 .c files, ~20-25 .h files, ~4000-6000 lines of C total for v1.

## License

Same as OpenClaw (Apache-2.0).
