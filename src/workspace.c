#include "workspace.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <time.h>

#define MAX_FILE_SIZE (64 * 1024)  /* 64KB max per workspace file */

char *ws_read_file(Arena *a, const char *workspace, const char *filename) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", workspace, filename);

    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0 || size > MAX_FILE_SIZE) {
        fclose(fp);
        return NULL;
    }

    char *buf = arena_alloc(a, (size_t)size + 1);
    size_t n = fread(buf, 1, (size_t)size, fp);
    buf[n] = '\0';
    fclose(fp);
    return buf;
}

/* Append a workspace file section to the prompt buffer. */
static void inject_file(Arena *a, char **out, size_t *out_len, size_t *out_cap,
                         const char *workspace, const char *filename) {
    char *content = ws_read_file(a, workspace, filename);
    int n;
    char header[256];

    if (content && content[0]) {
        n = snprintf(header, sizeof(header), "### %s\n\n", filename);
        /* Grow output if needed */
        size_t needed = *out_len + (size_t)n + strlen(content) + 4;
        while (needed > *out_cap) {
            *out_cap *= 2;
            /* Can't realloc arena — this is fine, we over-allocate */
        }
        memcpy(*out + *out_len, header, (size_t)n);
        *out_len += (size_t)n;
        size_t clen = strlen(content);
        memcpy(*out + *out_len, content, clen);
        *out_len += clen;
        memcpy(*out + *out_len, "\n\n", 2);
        *out_len += 2;
    } else {
        n = snprintf(header, sizeof(header), "### %s\n\n[File not found: %s]\n\n", filename, filename);
        memcpy(*out + *out_len, header, (size_t)n);
        *out_len += (size_t)n;
    }
}

char *ws_build_system_prompt(Arena *a, const char *workspace, const char *model) {
    size_t cap = 128 * 1024; /* 128KB prompt buffer */
    char *out = arena_alloc(a, cap);
    size_t len = 0;

    /* Safety */
    const char *safety =
        "## Safety\n\n"
        "- Do not exfiltrate private data.\n"
        "- Do not run destructive commands without asking.\n"
        "- Prefer recoverable operations over destructive ones.\n"
        "- When in doubt, ask before acting externally.\n\n";
    size_t slen = strlen(safety);
    memcpy(out + len, safety, slen);
    len += slen;

    /* Tools */
    const char *tools =
        "## Tools\n\n"
        "You have access to the following tools:\n\n"
        "- **shell**: Execute terminal commands\n"
        "- **file_read**: Read file contents\n"
        "- **file_write**: Write file contents\n"
        "- **memory_store**: Save to memory\n"
        "- **memory_recall**: Search memory\n\n";
    size_t tlen = strlen(tools);
    memcpy(out + len, tools, tlen);
    len += tlen;

    /* Workspace path */
    int n = snprintf(out + len, cap - len, "## Workspace\n\nWorking directory: `%s`\n\n", workspace);
    len += (size_t)n;

    /* Project context */
    const char *ctx = "## Project Context\n\n";
    size_t clen = strlen(ctx);
    memcpy(out + len, ctx, clen);
    len += clen;

    /* Identity files */
    const char *files[] = {
        "AGENTS.md", "SOUL.md", "TOOLS.md", "IDENTITY.md",
        "USER.md", "HEARTBEAT.md", "MEMORY.md", NULL
    };
    for (int i = 0; files[i]; i++) {
        inject_file(a, &out, &len, &cap, workspace, files[i]);
    }

    /* Date/time */
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    n = snprintf(out + len, cap - len,
        "## Current Date & Time\n\nTimezone: UTC\nDate: %04d-%02d-%02d %02d:%02d:%02d\n\n",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    len += (size_t)n;

    /* Runtime */
    char hostname[256] = "unknown";
    gethostname(hostname, sizeof(hostname));
    struct utsname uts;
    uname(&uts);
    n = snprintf(out + len, cap - len,
        "## Runtime\n\nHost: %s | OS: %s %s | Model: %s | Engine: CClaw (C)\n\n",
        hostname, uts.sysname, uts.machine, model);
    len += (size_t)n;

    out[len] = '\0';
    return out;
}
