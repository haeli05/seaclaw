#ifndef CCLAW_TOOLS_H
#define CCLAW_TOOLS_H

#include <cJSON.h>

/* Tool result */
typedef struct {
    int   success;
    char *output;
} ToolExecResult;

/* Execute a tool by name. Caller frees result.output. */
ToolExecResult tool_execute(const char *name, const char *input_json, const char *workspace);

/* Get tool definitions as JSON array string (for Anthropic API). */
char *tools_get_definitions(void);

#endif
