#ifndef CCLAW_ARENA_H
#define CCLAW_ARENA_H

#include <stddef.h>

/* Simple bump allocator for per-request allocations.
 * Avoids malloc/free churn in hot paths. */

typedef struct {
    char  *buf;
    size_t cap;
    size_t pos;
} Arena;

Arena arena_new(size_t cap);
void *arena_alloc(Arena *a, size_t size);
char *arena_strdup(Arena *a, const char *s);
char *arena_sprintf(Arena *a, const char *fmt, ...);
void  arena_reset(Arena *a);
void  arena_free(Arena *a);

#endif
