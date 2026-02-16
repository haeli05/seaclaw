# Configuration Guide

CClaw is configured through a combination of config files and environment variables. Environment variables always override file settings.

## Config File

Default location: `~/.cclaw/config`

Override with: `cclaw --config /path/to/config`

### Format

Simple key=value format (TOML-ish). Lines starting with `#` or `[` are ignored.

```ini
# CClaw Configuration
workspace = "/home/user/my-agent"
provider = "anthropic"
api_key = "sk-ant-api03-..."
model = "claude-sonnet-4-20250514"
temperature = 0.7

# Telegram
telegram_enabled = true
telegram_token = "123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11"
telegram_allowed = "12345678,87654321,*"

# Gateway
gateway_port = 3578
gateway_token = "my-secret-token"

# Memory
memory_db = "memory.db"

# Logging (0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL)
log_level = 2
```

### All Config Keys

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `workspace` | string | current directory | Path to workspace with identity files |
| `provider` | string | `"anthropic"` | LLM provider: `"anthropic"` or `"openai"` |
| `api_key` | string | *(none)* | API key for the selected provider |
| `model` | string | `"claude-sonnet-4-20250514"` | Model identifier |
| `temperature` | float | `0.7` | Sampling temperature (0.0–1.0) |
| `telegram_enabled` | bool | `false` | Enable Telegram bot |
| `telegram_token` | string | *(none)* | Telegram Bot API token |
| `telegram_allowed` | string | *(empty = allow all)* | Comma-separated user IDs/usernames. `*` allows all |
| `gateway_port` | int | `3578` | WebSocket gateway port (0 to disable) |
| `gateway_token` | string | *(none)* | Auth token for WebSocket connections |
| `memory_db` | string | `"memory.db"` | Path to SQLite memory database |
| `log_level` | int | `2` | Minimum log level (0=TRACE..5=FATAL) |

## Environment Variables

| Variable | Overrides | Notes |
|----------|-----------|-------|
| `CCLAW_WORKSPACE` | `workspace` | |
| `CCLAW_API_KEY` | `api_key` | |
| `ANTHROPIC_API_KEY` | `api_key` | Fallback if `CCLAW_API_KEY` not set |
| `OPENAI_API_KEY` | `api_key` | Fallback; also auto-sets `provider = "openai"` |
| `CCLAW_PROVIDER` | `provider` | |
| `CCLAW_MODEL` | `model` | |
| `CCLAW_TELEGRAM_TOKEN` | `telegram_token` | Also sets `telegram_enabled = true` |
| `CCLAW_LOG_LEVEL` | `log_level` | |

### Priority Order

1. Command-line arguments (`--model`, `--workspace`, `--gateway-port`)
2. Environment variables
3. Config file
4. Built-in defaults

## Command-Line Arguments

```
cclaw [options] [prompt]

Options:
  --help, -h              Show usage
  --version, -v           Print version
  --config <file>         Config file path
  --workspace <dir>       Workspace directory
  --model <model>         Override model name
  --telegram              Start Telegram bot mode
  --gateway-port <port>   WebSocket gateway port
```

## Workspace Directory

The workspace is where CClaw looks for identity files to build the system prompt:

```
workspace/
├── AGENTS.md       # Agent instructions and behavior
├── SOUL.md         # Core identity/personality
├── TOOLS.md        # Tool usage notes
├── IDENTITY.md     # Identity details
├── USER.md         # Info about the user
├── HEARTBEAT.md    # Heartbeat checklist
├── MEMORY.md       # Long-term memory
└── .cclaw/
    └── sessions/   # Persisted conversation sessions
        ├── cli.json
        ├── tg_12345.json
        └── ws_5.json
```

All files are optional. Missing files are noted in the system prompt as `[File not found]`.

## Telegram Access Control

The `telegram_allowed` field controls who can interact with the bot:

- **Empty string**: Allow everyone (default)
- **`*`**: Allow everyone (explicit)
- **User IDs**: `"12345678,87654321"` — only these Telegram user IDs
- **Usernames**: `"johndoe,janedoe"` — match by username
- **Mixed**: `"12345678,johndoe,*"` — first match wins

Blocked users are logged at WARN level but receive no response.

## Minimal Setup

```bash
export ANTHROPIC_API_KEY=sk-ant-api03-...
./cclaw "Hello"
```

That's it. Everything else has sensible defaults.
