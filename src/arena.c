#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

Arena arena_new(size_t cap) {
    Arena a = {0};
    a.buf = malloc(cap);
    a.cap = cap;
    a.pos = 0;
    return a;
}

void *arena_alloc(Arena *a, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;
    if (a->pos + size > a->cap) {
        /* Double capacity */
        size_t new_cap = a->cap * 2;
        while (a->pos + size > new_cap) new_cap *= 2;
        a->buf = realloc(a->buf, new_cap);
        a->cap = new_cap;
    }
    void *ptr = a->buf + a->pos;
    a->pos += size;
    return ptr;
}

char *arena_strdup(Arena *a, const char *s) {
    size_t len = strlen(s) + 1;
    char *p = arena_alloc(a, len);
    memcpy(p, s, len);
    return p;
}

char *arena_sprintf(Arena *a, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *p = arena_alloc(a, (size_t)n + 1);
    vsnprintf(p, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return p;
}

void arena_reset(Arena *a) { a->pos = 0; }

void arena_free(Arena *a) {
    free(a->buf);
    a->buf = NULL;
    a->cap = 0;
    a->pos = 0;
}
