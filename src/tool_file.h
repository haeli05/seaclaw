#ifndef CCLAW_TOOL_FILE_H
#define CCLAW_TOOL_FILE_H

#include "tools.h"

ToolExecResult tool_file_read(const char *input_json, const char *workspace);
ToolExecResult tool_file_write(const char *input_json, const char *workspace);

#endif
