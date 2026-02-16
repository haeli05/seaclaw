# Contributing to CClaw (SeaClaw)

## Getting Started

1. Fork the repository
2. Clone and build (see [BUILDING.md](BUILDING.md))
3. Create a feature branch: `git checkout -b feature/my-feature`
4. Make changes, test, commit
5. Push and open a Pull Request

## Code Style

### C Standards
- **C11** (`-std=c11`) — use C11 features freely
- **Warnings**: `-Wall -Wextra -Wpedantic` must compile clean
- **Formatting**: 4-space indentation, opening brace on same line

### Naming Conventions

| Entity | Convention | Example |
|--------|-----------|---------|
| Functions | `module_action` | `config_load()`, `arena_alloc()` |
| Types | `PascalCase` | `CClawConfig`, `ChatResponse` |
| Constants | `UPPER_SNAKE` | `MAX_TOKENS`, `CRON_MAX_JOBS` |
| Local vars | `snake_case` | `embed_dim`, `client_fd` |
| Macros | `UPPER_SNAKE` | `LOG_INFO(...)` |

### Include Guards

```c
#ifndef CCLAW_MODULENAME_H
#define CCLAW_MODULENAME_H
// ...
#endif
```

### Memory Ownership

Always document who frees what:
- If a function returns allocated memory, note "Caller frees" or "Caller must call X_free()"
- Use `ToolExecResult.output` pattern: caller always frees `output`
- Use arena allocator for request-scoped allocations

### Error Handling

- Return `-1` or `NULL` on error
- Log errors with `LOG_ERROR()` including context
- Never silently swallow errors

## Project Structure

```
src/
├── main.c              # Entry point — add new CLI flags and modes here
├── config.{c,h}        # Add new config keys here
├── provider*.{c,h}     # LLM providers — one file pair per provider
├── tool_*.{c,h}        # Tools — one file pair per tool
├── tools.{c,h}         # Tool registry — update dispatch + definitions
├── telegram.{c,h}      # Channel: Telegram
├── ws.{c,h}            # Channel: WebSocket gateway
├── session.{c,h}       # Conversation history
├── memory.{c,h}        # Persistent memory (SQLite)
├── cron.{c,h}          # Background scheduler
├── http.{c,h}          # HTTP/TLS client
├── arena.{c,h}         # Bump allocator
├── workspace.{c,h}     # System prompt builder
└── log.{c,h}           # Logging
```

## Adding Features

### New Tool
See [TOOLS.md](TOOLS.md) → "Adding a Custom Tool"

### New Provider
See [PROVIDERS.md](PROVIDERS.md) → "Adding a New Provider"

### New Channel
See [CHANNELS.md](CHANNELS.md) → "Adding a New Channel"

## Dependencies

CClaw is intentionally minimal. Think carefully before adding dependencies:

- **Vendored only**: All deps live in `deps/` and are committed or fetched at build time
- **No package managers**: No apt/brew/vcpkg dependencies beyond system libs
- **Currently**: cJSON, mbedTLS, SQLite3 (system lib)

If you need a new dependency, justify it in your PR description.

## Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add Discord channel support
fix: handle empty Telegram messages
docs: update API reference for memory module
refactor: extract URL parsing from http.c
chore: update mbedTLS to v2.28.9
```

## Testing

```bash
make test
```

Tests live in `tests/` and run via `tests/run_tests.sh`.

When adding features:
- Add corresponding test cases
- Ensure `make test` passes
- Test with both Anthropic and OpenAI providers if touching provider code

## Pull Request Checklist

- [ ] Compiles with `make` (no warnings)
- [ ] `make test` passes
- [ ] New functions documented in headers
- [ ] Memory ownership is clear (who frees what)
- [ ] Config changes added to both `config_load()` and `config_load_env()`
- [ ] README.md updated if adding user-visible features

## License

Apache-2.0. By contributing, you agree your contributions are licensed under the same terms.
