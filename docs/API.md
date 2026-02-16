# API Reference

Complete reference for all public functions in CClaw.

---

## Arena Allocator (`arena.h`)

### `Arena arena_new(size_t cap)`
Create a new arena with initial capacity.

**Parameters:**
- `cap` — Initial buffer size in bytes

**Returns:** `Arena` struct (stack-allocated)

**Example:**
```c
Arena a = arena_new(64 * 1024);  // 64KB
char *s = arena_strdup(&a, "hello");
arena_free(&a);
```

### `void *arena_alloc(Arena *a, size_t size)`
Allocate `size` bytes from the arena. Automatically doubles capacity if needed. All allocations are 8-byte aligned.

**Parameters:**
- `a` — Arena pointer
- `size` — Bytes to allocate

**Returns:** Pointer to allocated memory

### `char *arena_strdup(Arena *a, const char *s)`
Duplicate a string into arena memory.

**Parameters:**
- `a` — Arena pointer
- `s` — Null-terminated string to copy

**Returns:** New string in arena memory

### `char *arena_sprintf(Arena *a, const char *fmt, ...)`
Printf-style formatted string allocated from arena.

**Parameters:**
- `a` — Arena pointer
- `fmt` — Format string
- `...` — Format arguments

**Returns:** Formatted string in arena memory

### `void arena_reset(Arena *a)`
Reset position to 0 (reuse memory without freeing).

### `void arena_free(Arena *a)`
Free the arena's backing buffer.

---

## Configuration (`config.h`)

### `void config_defaults(CClawConfig *cfg)`
Initialize config struct with default values.

**Defaults:**
| Field | Default |
|-------|---------|
| `provider` | `"anthropic"` |
| `model` | `"claude-sonnet-4-20250514"` |
| `temperature` | `0.7` |
| `gateway_port` | `3578` |
| `log_level` | `2` (INFO) |
| `memory_db` | `"memory.db"` |

### `int config_load(CClawConfig *cfg, const char *path)`
Parse a key=value config file.

**Parameters:**
- `cfg` — Config struct to populate
- `path` — Path to config file

**Returns:** `0` on success, `-1` on failure

**Supported keys:** `workspace`, `provider`, `api_key`, `model`, `temperature`, `telegram_token`, `telegram_allowed`, `telegram_enabled`, `gateway_port`, `gateway_token`, `memory_db`, `log_level`

### `void config_load_env(CClawConfig *cfg)`
Override config from environment variables. Called after `config_load()`.

**Environment variables:** `CCLAW_WORKSPACE`, `CCLAW_API_KEY`, `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `CCLAW_PROVIDER`, `CCLAW_MODEL`, `CCLAW_TELEGRAM_TOKEN`, `CCLAW_LOG_LEVEL`

### `void config_dump(const CClawConfig *cfg)`
Print config summary to log (INFO level). API key is masked.

---

## Cron Scheduler (`cron.h`)

### `void cron_init(CronScheduler *sched)`
Initialize a scheduler (zeroes all fields).

### `int cron_parse(const char *expr_str, CronExpr *expr)`
Parse a 5-field cron expression string.

**Parameters:**
- `expr_str` — Cron expression (e.g., `"*/5 * * * *"`)
- `expr` — Output expression struct

**Returns:** `0` on success, `-1` on parse error

**Supported syntax:**
- Exact value: `30` (minute 30)
- Wildcard: `*` (every)
- Step: `*/N` (every N)

### `int cron_add(CronScheduler *sched, const char *name, const char *expr_str, CronJobFn fn, void *userdata)`
Add a named job to the scheduler.

**Parameters:**
- `sched` — Scheduler
- `name` — Job name (max 63 chars)
- `expr_str` — Cron expression
- `fn` — Callback function `void (*)(void *)`
- `userdata` — Passed to callback

**Returns:** Job index, or `-1` on failure (max 64 jobs)

**Example:**
```c
void my_job(void *ud) { printf("tick!\n"); }

CronScheduler sched;
cron_init(&sched);
cron_add(&sched, "heartbeat", "*/30 * * * *", my_job, NULL);
```

### `bool cron_remove(CronScheduler *sched, const char *name)`
Deactivate a job by name.

**Returns:** `true` if found and removed

### `void cron_run(CronScheduler *sched)`
Start the scheduler loop (**blocking**). Checks every 30 seconds. Run in a thread.

### `void cron_stop(CronScheduler *sched)`
Signal the scheduler to stop. Returns from `cron_run()` within ~30 seconds.

### `bool cron_matches(const CronExpr *expr, const struct tm *tm)`
Test if a cron expression matches a given time.

---

## HTTP Client (`http.h`)

### `HttpClient *http_client_new(void)`
Create an HTTP client with TLS context (loads system CA certs from `/etc/ssl/certs`).

**Returns:** Client pointer, or `NULL` on failure. Caller must call `http_client_free()`.

### `void http_client_free(HttpClient *c)`
Free the HTTP client and TLS context.

### `HttpResponse http_post_json(HttpClient *c, const char *url, const char *body, const char **headers, int num_headers)`
Send an HTTPS POST with JSON body.

**Parameters:**
- `c` — HTTP client
- `url` — Full HTTPS URL
- `body` — JSON request body string
- `headers` — Array of key-value pairs: `{"Key1", "Val1", "Key2", "Val2", ...}`
- `num_headers` — Number of header **pairs** (not array length)

**Returns:** `HttpResponse` with `status`, `body`, `body_len`. Caller must call `http_response_free()`.

### `HttpResponse http_get(HttpClient *c, const char *url, const char **headers, int num_headers)`
Send an HTTPS GET request. Same parameter conventions as `http_post_json`.

### `int http_post_stream(HttpClient *c, const char *url, const char *body, const char **headers, int num_headers, HttpStreamCb cb, void *userdata)`
POST with SSE streaming response. Calls `cb` for each `data:` line.

**Parameters:**
- `cb` — `bool (*)(const char *chunk, size_t len, void *userdata)`. Return `false` to abort.
- `userdata` — Passed to callback

**Returns:** `0` on success, `-1` on error

### `void http_response_free(HttpResponse *r)`
Free response body and headers.

---

## Logging (`log.h`)

### `void log_set_level(LogLevel level)`
Set minimum log level. Values: `LOG_TRACE` (0) through `LOG_FATAL` (5).

### `void log_set_file(FILE *fp)`
Redirect log output to a file. Default: `stderr`.

### Log Macros
```c
LOG_TRACE("detail: %d", val);
LOG_DEBUG("debug info: %s", str);
LOG_INFO("started on port %d", port);
LOG_WARN("retrying connection");
LOG_ERROR("failed: %s", strerror(errno));
LOG_FATAL("cannot continue");
```

Each macro expands to `cc_log(level, __FILE__, __LINE__, fmt, ...)`.

**Output format:** `2024-01-15T12:30:00Z INFO  main.c:42 Message here`

---

## Memory (`memory.h`)

### `Memory *memory_open(const char *db_path)`
Open or create a SQLite memory database. Creates the `memory` table if absent.

**Returns:** `Memory` pointer, or `NULL` on failure. Caller must call `memory_close()`.

### `void memory_close(Memory *m)`
Close the database and free resources.

### `bool memory_store(Memory *m, const char *key, const char *value, const float *embedding, int embed_dim)`
Store a key-value pair with optional embedding vector.

**Parameters:**
- `key` — Unique key (used for INSERT OR REPLACE)
- `value` — Text value
- `embedding` — Float array (can be `NULL`)
- `embed_dim` — Dimension count (0 if no embedding)

**Returns:** `true` on success

### `int memory_search(Memory *m, const float *query_embedding, int embed_dim, int top_k, MemoryResult *results)`
Find top-k entries by cosine similarity against the query embedding.

**Parameters:**
- `query_embedding` — Query vector
- `embed_dim` — Dimension (must match stored dimensions)
- `top_k` — Maximum results to return
- `results` — Pre-allocated array of `MemoryResult`

**Returns:** Number of results found. Caller must call `memory_results_free()`.

### `char *memory_get(Memory *m, const char *key)`
Direct key lookup. Returns `NULL` if not found. **Caller frees.**

### `bool memory_delete(Memory *m, const char *key)`
Delete an entry by key.

### `void memory_results_free(MemoryResult *results, int count)`
Free the key/value strings in a results array.

---

## Provider — Anthropic (`provider.h`)

### `ChatResponse provider_chat(HttpClient *http, const char *api_key, const char *model, const char *system_prompt, const char *messages_json, const char *tools_json, float temperature)`
Send a non-streaming chat request to the Anthropic Messages API.

**Parameters:**
- `api_key` — Anthropic API key
- `model` — Model ID (e.g., `"claude-sonnet-4-20250514"`)
- `system_prompt` — System prompt text
- `messages_json` — JSON array of `{role, content}` messages
- `tools_json` — JSON array of tool definitions (Anthropic format), or `NULL`
- `temperature` — Sampling temperature

**Returns:** `ChatResponse` with `text`, `tool_calls`, `num_tools`, `stop_reason`, token counts. Caller must call `chat_response_free()`.

### `ChatResponse provider_chat_stream(HttpClient *http, ...same params..., StreamTextCb cb, void *userdata)`
Streaming variant. Calls `cb` with text deltas as they arrive. Returns the accumulated `ChatResponse` (including any tool calls).

### `void chat_response_free(ChatResponse *r)`
Free all strings and tool call arrays in a response.

---

## Provider — OpenAI (`provider_openai.h`)

### `ChatResponse openai_chat(HttpClient *http, const char *api_key, const char *model, const char *system_prompt, const char *messages_json, const char *tools_json, float temperature)`
Non-streaming chat via OpenAI Chat Completions API. Automatically converts Anthropic-format tool definitions to OpenAI function calling format.

### `ChatResponse openai_chat_stream(HttpClient *http, ...same params..., StreamTextCb cb, void *userdata)`
Streaming variant for OpenAI. Handles incremental tool call assembly across SSE events.

---

## Session (`session.h`)

### `Session *session_new(const char *workspace, const char *session_id)`
Create or load a session. If `session_id` is `NULL`, creates an ephemeral (non-persisted) session.

**Storage path:** `{workspace}/.cclaw/sessions/{session_id}.json`

### `void session_add_user(Session *s, const char *text)`
Append a user message.

### `void session_add_assistant(Session *s, const char *text)`
Append an assistant text response (Anthropic content block format).

### `void session_add_tool_use(Session *s, const char *tool_id, const char *name, const char *input_json)`
Record a tool use. Appends to the last assistant message if it exists, otherwise creates a new one.

### `void session_add_tool_result(Session *s, const char *tool_id, const char *output)`
Record a tool result (as a user message with `tool_result` content block).

### `char *session_messages_json(Session *s)`
Serialize the message history to a JSON string. **Caller frees.**

### `void session_save(Session *s)`
Write session to disk (creates directories as needed).

### `void session_free(Session *s)`
Free session and its cJSON message array.

---

## Telegram (`telegram.h`)

### `int telegram_poll_loop(HttpClient *http, const CClawConfig *cfg, TelegramMsgHandler handler, void *userdata)`
Start long-polling the Telegram Bot API. **Blocking.** Calls `handler` for each incoming text message.

**Parameters:**
- `handler` — `char *(*)(const TelegramMessage *msg, void *userdata)`. Return allocated reply string, or `NULL` for no reply.

### `int telegram_send(HttpClient *http, const char *token, long long chat_id, const char *text)`
Send a text message with Markdown parse mode.

**Returns:** `0` on success, `-1` on failure

### `int telegram_send_typing(HttpClient *http, const char *token, long long chat_id)`
Send "typing..." indicator to a chat.

---

## Tools (`tools.h`)

### `ToolExecResult tool_execute(const char *name, const char *input_json, const char *workspace)`
Execute a tool by name. Dispatches to the appropriate handler.

**Known tools:** `"shell"`, `"file_read"`, `"file_write"`

**Returns:** `ToolExecResult` with `success` flag and `output` string. **Caller frees `output`.**

### `char *tools_get_definitions(void)`
Get tool definitions as a JSON array string (Anthropic input_schema format). **Caller frees.**

---

## Tool: File (`tool_file.h`)

### `ToolExecResult tool_file_read(const char *input_json, const char *workspace)`
Read a file. Input JSON: `{"path": "relative/or/absolute"}`. Max 512KB.

### `ToolExecResult tool_file_write(const char *input_json, const char *workspace)`
Write a file. Input JSON: `{"path": "...", "content": "..."}`. Creates parent directories.

---

## Tool: Shell (`tool_shell.h`)

### `ToolExecResult tool_shell_exec(const char *input_json, const char *workspace)`
Execute a shell command via `fork()`/`exec()` with piped stdout/stderr. Input JSON: `{"command": "ls -la"}`. Max 128KB output. Working directory set to `workspace`.

**Output format:** `[exit N]\n<stdout+stderr>`

---

## WebSocket Server (`ws.h`)

### `int ws_server_start(const WsServerConfig *cfg)`
Start a WebSocket server. **Blocking** — run in a thread. Uses `poll()` for multiplexing up to 64 clients.

### `int ws_send_text(int client_fd, const char *msg, size_t len)`
Send a text frame to a connected client.

### `int ws_send_close(int client_fd)`
Send a close frame.

### `int ws_handshake(int client_fd, const char *auth_token)`
Perform the HTTP→WebSocket upgrade handshake. Optionally validates `Authorization: Bearer <token>` header or `?token=` query parameter.

### `int ws_read_frame(int fd, WsFrame *frame)`
Read and decode one WebSocket frame (handles masking). **Caller frees `frame.payload`.**

### `int ws_write_frame(int fd, WsOpcode opcode, const char *data, size_t len)`
Write a WebSocket frame (server-to-client, unmasked).

---

## Workspace (`workspace.h`)

### `char *ws_read_file(Arena *a, const char *workspace, const char *filename)`
Read a workspace file into arena memory. Returns `NULL` if not found. Max 64KB.

### `char *ws_build_system_prompt(Arena *a, const char *workspace, const char *model)`
Build the full system prompt by reading identity files (`AGENTS.md`, `SOUL.md`, `TOOLS.md`, `IDENTITY.md`, `USER.md`, `HEARTBEAT.md`, `MEMORY.md`) and injecting runtime info (date/time, hostname, model name).

**Returns:** Prompt string allocated from arena `a`.
