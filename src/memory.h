#ifndef CCLAW_MEMORY_H
#define CCLAW_MEMORY_H

#include <stdbool.h>

typedef struct Memory Memory;

/* Search result */
typedef struct {
    char  *key;
    char  *value;
    float  score;   /* Cosine similarity (0..1) */
} MemoryResult;

/* Open/create SQLite memory database. */
Memory *memory_open(const char *db_path);
void    memory_close(Memory *m);

/* Store a key-value pair with an embedding vector. */
bool memory_store(Memory *m, const char *key, const char *value,
                  const float *embedding, int embed_dim);

/* Semantic search: find top-k entries closest to the query embedding. */
int memory_search(Memory *m, const float *query_embedding, int embed_dim,
                  int top_k, MemoryResult *results);

/* Simple text lookup by key. Caller frees returned string. */
char *memory_get(Memory *m, const char *key);

/* Delete by key. */
bool memory_delete(Memory *m, const char *key);

/* Free search results array contents. */
void memory_results_free(MemoryResult *results, int count);

#endif
