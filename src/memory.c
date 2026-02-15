/*
 * SQLite-backed memory with embedding-based semantic search.
 *
 * Stores key-value pairs alongside float embedding vectors (serialized as blobs).
 * Search computes cosine similarity in C against all stored embeddings.
 * For small-to-medium stores (<100k entries) this is fast enough without an index.
 */

#include "memory.h"
#include "log.h"
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct Memory {
    sqlite3 *db;
};

Memory *memory_open(const char *db_path) {
    Memory *m = calloc(1, sizeof(*m));
    int rc = sqlite3_open(db_path, &m->db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open memory DB %s: %s", db_path, sqlite3_errmsg(m->db));
        sqlite3_close(m->db);
        free(m);
        return NULL;
    }

    /* Create table */
    const char *sql =
        "CREATE TABLE IF NOT EXISTS memory ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL,"
        "  embedding BLOB,"
        "  embed_dim INTEGER DEFAULT 0,"
        "  created_at INTEGER DEFAULT (strftime('%s','now')),"
        "  updated_at INTEGER DEFAULT (strftime('%s','now'))"
        ");";

    char *err = NULL;
    rc = sqlite3_exec(m->db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to create memory table: %s", err);
        sqlite3_free(err);
        sqlite3_close(m->db);
        free(m);
        return NULL;
    }

    return m;
}

void memory_close(Memory *m) {
    if (!m) return;
    sqlite3_close(m->db);
    free(m);
}

bool memory_store(Memory *m, const char *key, const char *value,
                  const float *embedding, int embed_dim) {
    const char *sql =
        "INSERT OR REPLACE INTO memory (key, value, embedding, embed_dim, updated_at) "
        "VALUES (?, ?, ?, ?, strftime('%s','now'));";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_ERROR("memory_store prepare: %s", sqlite3_errmsg(m->db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);

    if (embedding && embed_dim > 0) {
        sqlite3_bind_blob(stmt, 3, embedding, embed_dim * (int)sizeof(float), SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, embed_dim);
    } else {
        sqlite3_bind_null(stmt, 3);
        sqlite3_bind_int(stmt, 4, 0);
    }

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) LOG_ERROR("memory_store step: %s", sqlite3_errmsg(m->db));
    sqlite3_finalize(stmt);
    return ok;
}

/* Cosine similarity between two vectors. */
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

int memory_search(Memory *m, const float *query_embedding, int embed_dim,
                  int top_k, MemoryResult *results) {
    const char *sql = "SELECT key, value, embedding, embed_dim FROM memory "
                      "WHERE embedding IS NOT NULL;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_ERROR("memory_search prepare: %s", sqlite3_errmsg(m->db));
        return 0;
    }

    /* Collect all results with scores, then pick top-k */
    typedef struct { char *key; char *value; float score; } Candidate;
    int cap = 256, count = 0;
    Candidate *cands = malloc((size_t)cap * sizeof(Candidate));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int stored_dim = sqlite3_column_int(stmt, 3);
        if (stored_dim != embed_dim) continue;

        const float *stored_emb = sqlite3_column_blob(stmt, 2);
        int blob_size = sqlite3_column_bytes(stmt, 2);
        if (!stored_emb || blob_size != embed_dim * (int)sizeof(float)) continue;

        float score = cosine_sim(query_embedding, stored_emb, embed_dim);

        if (count >= cap) {
            cap *= 2;
            cands = realloc(cands, (size_t)cap * sizeof(Candidate));
        }

        cands[count].key = strdup((const char *)sqlite3_column_text(stmt, 0));
        cands[count].value = strdup((const char *)sqlite3_column_text(stmt, 1));
        cands[count].score = score;
        count++;
    }
    sqlite3_finalize(stmt);

    /* Simple selection sort for top-k */
    int result_count = count < top_k ? count : top_k;
    for (int i = 0; i < result_count; i++) {
        int best = i;
        for (int j = i + 1; j < count; j++) {
            if (cands[j].score > cands[best].score) best = j;
        }
        if (best != i) {
            Candidate tmp = cands[i];
            cands[i] = cands[best];
            cands[best] = tmp;
        }
        results[i].key = cands[i].key;
        results[i].value = cands[i].value;
        results[i].score = cands[i].score;
    }

    /* Free unused candidates */
    for (int i = result_count; i < count; i++) {
        free(cands[i].key);
        free(cands[i].value);
    }
    free(cands);

    return result_count;
}

char *memory_get(Memory *m, const char *key) {
    const char *sql = "SELECT value FROM memory WHERE key = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);

    char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = strdup((const char *)sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return result;
}

bool memory_delete(Memory *m, const char *key) {
    const char *sql = "DELETE FROM memory WHERE key = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(m->db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

void memory_results_free(MemoryResult *results, int count) {
    for (int i = 0; i < count; i++) {
        free(results[i].key);
        free(results[i].value);
    }
}
