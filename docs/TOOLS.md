# Tool System

CClaw provides tools that the LLM can invoke to interact with the system. Tools are defined in Anthropic's `input_schema` format and dispatched through a central registry.

## Built-in Tools

### `shell`

Execute a shell command and capture stdout/stderr.

**Schema:**
```json
{
  "name": "shell",
  "description": "Execute a shell command and return stdout/stderr.",
  "input_schema": {
    "type": "object",
    "properties": {
      "command": { "type": "string", "description": "Shell command to execute" }
    },
    "required": ["command"]
  }
}
```

**Implementation** (`src/tool_shell.c`):
- Uses `fork()` + `execl("/bin/sh", "sh", "-c", command)` 
- Pipes stdout and stderr to parent via `pipe()`/`dup2()`
- Working directory set to the workspace path
- Maximum output: 128KB (truncated if exceeded)
- Output format: `[exit N]\n<combined stdout+stderr>`

**Example:**
```json
{"command": "ls -la /tmp"}
```
→ `[exit 0]\ntotal 28\ndrwxrwxrwt 7 root root ...\n`

### `file_read`

Read the contents of a file.

**Schema:**
```json
{
  "name": "file_read",
  "description": "Read the contents of a file.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": { "type": "string", "description": "File path (relative to workspace)" }
    },
    "required": ["path"]
  }
}
```

**Implementation** (`src/tool_file.c`):
- Relative paths resolved against workspace directory
- Absolute paths used as-is
- Maximum read size: 512KB
- Returns file contents as plain text

### `file_write`

Write content to a file, creating parent directories as needed.

**Schema:**
```json
{
  "name": "file_write",
  "description": "Write content to a file. Creates parent directories.",
  "input_schema": {
    "type": "object",
    "properties": {
      "path": { "type": "string", "description": "File path" },
      "content": { "type": "string", "description": "Content to write" }
    },
    "required": ["path", "content"]
  }
}
```

**Implementation:**
- Creates parent directories recursively (like `mkdir -p`)
- Overwrites existing files
- Returns: `"Wrote N bytes to path"`

## Tool Dispatch

The `tool_execute()` function in `src/tools.c` routes tool calls by name:

```c
ToolExecResult tool_execute(const char *name, const char *input_json, const char *workspace) {
    if (!strcmp(name, "shell"))      return tool_shell_exec(input_json, workspace);
    if (!strcmp(name, "file_read"))  return tool_file_read(input_json, workspace);
    if (!strcmp(name, "file_write")) return tool_file_write(input_json, workspace);
    // Unknown tool → error result
}
```

## Tool Execution Flow

```
LLM response with tool_use
  │
  ▼
agent_turn() in main.c
  ├── tool_execute(name, input_json, workspace)
  │     └── dispatches to tool_shell/tool_file
  ├── session_add_tool_use()       — record the call
  ├── session_add_tool_result()    — record the output
  └── Loop back to LLM with results (up to 10 turns)
```

## Adding a Custom Tool

### Step 1: Create tool files

```c
// src/tool_web.h
#ifndef CCLAW_TOOL_WEB_H
#define CCLAW_TOOL_WEB_H
#include "tools.h"

ToolExecResult tool_web_fetch(const char *input_json, const char *workspace);
#endif
```

```c
// src/tool_web.c
#include "tool_web.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

ToolExecResult tool_web_fetch(const char *input_json, const char *workspace) {
    ToolExecResult r = {0, NULL};
    (void)workspace;

    cJSON *args = cJSON_Parse(input_json);
    cJSON *url = cJSON_GetObjectItem(args, "url");

    if (!url || !url->valuestring) {
        r.output = strdup("Error: missing 'url'");
        cJSON_Delete(args);
        return r;
    }

    // ... implement fetch logic ...

    r.success = 1;
    r.output = strdup("fetched content here");
    cJSON_Delete(args);
    return r;
}
```

### Step 2: Register in `tools.c`

Add the dispatch:
```c
#include "tool_web.h"

ToolExecResult tool_execute(...) {
    // ... existing tools ...
    if (!strcmp(name, "web_fetch")) return tool_web_fetch(input_json, workspace);
}
```

Add the definition to `tools_get_definitions()`:
```c
"{"
    "\"name\":\"web_fetch\","
    "\"description\":\"Fetch a URL and return its content.\","
    "\"input_schema\":{"
        "\"type\":\"object\","
        "\"properties\":{"
            "\"url\":{\"type\":\"string\",\"description\":\"URL to fetch\"}"
        "},"
        "\"required\":[\"url\"]"
    "}"
"}"
```

### Step 3: Update Makefile

```makefile
SRCS = ... src/tool_web.c
```

## `ToolExecResult` Contract

```c
typedef struct {
    int   success;   // 1 = success, 0 = error
    char *output;    // Result text (caller frees with free())
} ToolExecResult;
```

- Always set `output` (even on error — describe what went wrong)
- Always set `success` to indicate pass/fail
- The caller (`agent_turn`) always calls `free(output)`
