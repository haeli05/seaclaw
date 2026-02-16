# Memory System

CClaw includes a SQLite-backed memory store with embedding-based semantic search. It allows the agent to persist and retrieve information across sessions.

## Overview

The memory system stores key-value pairs alongside optional float embedding vectors. Search computes cosine similarity in C against all stored embeddings — no external vector database needed.

**File:** `src/memory.c`, `src/memory.h`

## Database Schema

SQLite database at the path specified by `memory_db` config (default: `memory.db`):

```sql
CREATE TABLE IF NOT EXISTS memory (
    key         TEXT PRIMARY KEY,
    value       TEXT NOT NULL,
    embedding   BLOB,              -- float[] serialized as raw bytes
    embed_dim   INTEGER DEFAULT 0, -- dimensionality of the embedding
    created_at  INTEGER DEFAULT (strftime('%s','now')),
    updated_at  INTEGER DEFAULT (strftime('%s','now'))
);
```

## Usage

### Opening and Closing

```c
#include "memory.h"

Memory *m = memory_open("memory.db");
if (!m) {
    // handle error
}

// ... use memory ...

memory_close(m);
```

### Storing Data

```c
// Without embedding (key-value only)
memory_store(m, "user_name", "Alice", NULL, 0);

// With embedding (for semantic search)
float embedding[1536] = { /* ... from embedding API ... */ };
memory_store(m, "fact_1", "The user likes coffee", embedding, 1536);
```

Storing with an existing key performs `INSERT OR REPLACE` — the value and embedding are updated, and `updated_at` is refreshed.

### Exact Key Lookup

```c
char *value = memory_get(m, "user_name");
if (value) {
    printf("Name: %s\n", value);
    free(value);  // caller frees
}
```

### Semantic Search

```c
float query_embedding[1536] = { /* ... */ };
MemoryResult results[10];

int count = memory_search(m, query_embedding, 1536, 10, results);

for (int i = 0; i < count; i++) {
    printf("%.3f  %s: %s\n",
           results[i].score,   // cosine similarity (0..1)
           results[i].key,
           results[i].value);
}

memory_results_free(results, count);
```

### Deletion

```c
memory_delete(m, "old_fact");
```

## Cosine Similarity

Search computes cosine similarity between the query embedding and every stored embedding:

```c
// From memory.c
static float cosine_sim(const float *a, const float *b, int n) {
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (int i = 0; i < n; i++) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }
    float denom = sqrtf(na) * sqrtf(nb);
    return denom > 0.0f ? dot / denom : 0.0f;
}
```

Results are sorted by score (highest first) using selection sort, then the top-k are returned.

## Performance Characteristics

- **Storage:** O(1) per store/get/delete operation (SQLite indexed by primary key)
- **Search:** O(n × d) where n = number of entries, d = embedding dimension
  - Linear scan of all embeddings — no approximate nearest neighbor index
  - For <100k entries with typical dimensions (768–1536), this is fast enough (<100ms)
- **Memory:** Embeddings are stored as BLOBs in SQLite, not held in memory

## Embedding Integration

CClaw's memory module stores and searches embeddings but does **not** generate them. To use semantic search, you need to:

1. Call an embedding API (e.g., OpenAI `text-embedding-3-small`, Anthropic Voyage) externally
2. Pass the resulting float vector to `memory_store()` and `memory_search()`

The system prompt mentions `memory_store` and `memory_recall` as available tools, but these are not yet wired into the tool dispatch in `tools.c` — this is a planned feature.

## Configuration

| Config Key | Default | Description |
|-----------|---------|-------------|
| `memory_db` | `"memory.db"` | Path to SQLite database file |

The database is created automatically on first `memory_open()` if it doesn't exist.
