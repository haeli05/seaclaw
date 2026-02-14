#ifndef CCLAW_WORKSPACE_H
#define CCLAW_WORKSPACE_H

#include "arena.h"

/* Read a workspace file into arena memory. Returns NULL if not found. */
char *ws_read_file(Arena *a, const char *workspace, const char *filename);

/* Build the full system prompt from workspace identity files.
 * Caller must arena_free the arena when done. */
char *ws_build_system_prompt(Arena *a, const char *workspace, const char *model);

#endif
