#include "tool_file.h"
#include "log.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

#define MAX_FILE_READ (512 * 1024)  /* 512KB */

static void resolve_path(char *out, size_t outlen, const char *workspace, const char *path) {
    if (path[0] == '/') {
        strncpy(out, path, outlen - 1);
    } else {
        snprintf(out, outlen, "%s/%s", workspace, path);
    }
}

/* Recursively create directories. */
static int mkdirs(const char *path) {
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    char *dir = dirname(tmp);

    /* Check if exists */
    struct stat st;
    if (stat(dir, &st) == 0) return 0;

    /* Recurse */
    mkdirs(dir);
    return mkdir(dir, 0755);
}

ToolExecResult tool_file_read(const char *input_json, const char *workspace) {
    ToolExecResult r = {0, NULL};

    cJSON *args = cJSON_Parse(input_json);
    if (!args) { r.output = strdup("Error: invalid JSON"); return r; }

    cJSON *path = cJSON_GetObjectItem(args, "path");
    if (!path || !path->valuestring) {
        r.output = strdup("Error: missing 'path'");
        cJSON_Delete(args);
        return r;
    }

    char fullpath[1024];
    resolve_path(fullpath, sizeof(fullpath), workspace, path->valuestring);

    FILE *fp = fopen(fullpath, "r");
    if (!fp) {
        char buf[1280];
        snprintf(buf, sizeof(buf), "Error: cannot read %s: %s", fullpath, strerror(errno));
        r.output = strdup(buf);
        cJSON_Delete(args);
        return r;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size > MAX_FILE_READ) size = MAX_FILE_READ;

    r.output = malloc((size_t)size + 1);
    size_t n = fread(r.output, 1, (size_t)size, fp);
    r.output[n] = '\0';
    r.success = 1;

    fclose(fp);
    cJSON_Delete(args);
    return r;
}

ToolExecResult tool_file_write(const char *input_json, const char *workspace) {
    ToolExecResult r = {0, NULL};

    cJSON *args = cJSON_Parse(input_json);
    if (!args) { r.output = strdup("Error: invalid JSON"); return r; }

    cJSON *path = cJSON_GetObjectItem(args, "path");
    cJSON *content = cJSON_GetObjectItem(args, "content");
    if (!path || !path->valuestring || !content || !content->valuestring) {
        r.output = strdup("Error: missing 'path' or 'content'");
        cJSON_Delete(args);
        return r;
    }

    char fullpath[1024];
    resolve_path(fullpath, sizeof(fullpath), workspace, path->valuestring);

    /* Create parent directories */
    mkdirs(fullpath);

    FILE *fp = fopen(fullpath, "w");
    if (!fp) {
        char buf[1280];
        snprintf(buf, sizeof(buf), "Error: cannot write %s: %s", fullpath, strerror(errno));
        r.output = strdup(buf);
        cJSON_Delete(args);
        return r;
    }

    size_t len = strlen(content->valuestring);
    fwrite(content->valuestring, 1, len, fp);
    fclose(fp);

    char buf[1280];
    snprintf(buf, sizeof(buf), "Wrote %zu bytes to %s", len, path->valuestring);
    r.output = strdup(buf);
    r.success = 1;

    LOG_INFO("file_write: %s (%zu bytes)", path->valuestring, len);

    cJSON_Delete(args);
    return r;
}
