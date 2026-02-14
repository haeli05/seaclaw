#include "tools.h"
#include "tool_shell.h"
#include "tool_file.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

ToolExecResult tool_execute(const char *name, const char *input_json, const char *workspace) {
    ToolExecResult r = {0, NULL};

    if (!strcmp(name, "shell")) {
        r = tool_shell_exec(input_json, workspace);
    } else if (!strcmp(name, "file_read")) {
        r = tool_file_read(input_json, workspace);
    } else if (!strcmp(name, "file_write")) {
        r = tool_file_write(input_json, workspace);
    } else {
        LOG_WARN("Unknown tool: %s", name);
        r.success = 0;
        char buf[256];
        snprintf(buf, sizeof(buf), "Unknown tool: %s", name);
        r.output = strdup(buf);
    }

    return r;
}

char *tools_get_definitions(void) {
    return strdup(
        "["
        "{"
            "\"name\":\"shell\","
            "\"description\":\"Execute a shell command and return stdout/stderr.\","
            "\"input_schema\":{"
                "\"type\":\"object\","
                "\"properties\":{"
                    "\"command\":{\"type\":\"string\",\"description\":\"Shell command to execute\"}"
                "},"
                "\"required\":[\"command\"]"
            "}"
        "},"
        "{"
            "\"name\":\"file_read\","
            "\"description\":\"Read the contents of a file.\","
            "\"input_schema\":{"
                "\"type\":\"object\","
                "\"properties\":{"
                    "\"path\":{\"type\":\"string\",\"description\":\"File path (relative to workspace)\"}"
                "},"
                "\"required\":[\"path\"]"
            "}"
        "},"
        "{"
            "\"name\":\"file_write\","
            "\"description\":\"Write content to a file. Creates parent directories.\","
            "\"input_schema\":{"
                "\"type\":\"object\","
                "\"properties\":{"
                    "\"path\":{\"type\":\"string\",\"description\":\"File path\"},"
                    "\"content\":{\"type\":\"string\",\"description\":\"Content to write\"}"
                "},"
                "\"required\":[\"path\",\"content\"]"
            "}"
        "}"
        "]"
    );
}
