#ifndef CCLAW_SESSION_H
#define CCLAW_SESSION_H

#include <cJSON.h>

/* In-memory message history as a JSON array.
 * Serialized to/from disk as JSONL. */
typedef struct {
    cJSON *messages;  /* JSON array of {role, content} objects */
    int    count;
    char   session_file[512];
} Session;

Session *session_new(const char *workspace, const char *session_id);
void     session_add_user(Session *s, const char *text);
void     session_add_assistant(Session *s, const char *text);
void     session_add_tool_use(Session *s, const char *tool_id, const char *name, const char *input_json);
void     session_add_tool_result(Session *s, const char *tool_id, const char *output);
char    *session_messages_json(Session *s);  /* Caller frees */
void     session_save(Session *s);
void     session_free(Session *s);

#endif
