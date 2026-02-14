#include "tool_shell.h"
#include "log.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_OUTPUT (128 * 1024)  /* 128KB max output */
#define TIMEOUT_SECS 30

ToolExecResult tool_shell_exec(const char *input_json, const char *workspace) {
    ToolExecResult r = {0, NULL};

    cJSON *args = cJSON_Parse(input_json);
    if (!args) {
        r.output = strdup("Error: invalid JSON input");
        return r;
    }

    cJSON *cmd = cJSON_GetObjectItem(args, "command");
    if (!cmd || !cmd->valuestring) {
        r.output = strdup("Error: missing 'command' parameter");
        cJSON_Delete(args);
        return r;
    }

    LOG_INFO("shell: %s", cmd->valuestring);

    /* Fork and exec with pipe */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        r.output = strdup("Error: pipe() failed");
        cJSON_Delete(args);
        return r;
    }

    pid_t pid = fork();
    if (pid < 0) {
        r.output = strdup("Error: fork() failed");
        close(pipefd[0]);
        close(pipefd[1]);
        cJSON_Delete(args);
        return r;
    }

    if (pid == 0) {
        /* Child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (workspace && workspace[0]) chdir(workspace);

        execl("/bin/sh", "sh", "-c", cmd->valuestring, (char *)NULL);
        _exit(127);
    }

    /* Parent */
    close(pipefd[1]);

    char *buf = malloc(MAX_OUTPUT);
    size_t total = 0;
    ssize_t n;

    while ((n = read(pipefd[0], buf + total, MAX_OUTPUT - total - 1)) > 0) {
        total += (size_t)n;
        if (total >= MAX_OUTPUT - 1) break;
    }
    close(pipefd[0]);
    buf[total] = '\0';

    int status;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    /* Build result */
    size_t result_size = total + 64;
    r.output = malloc(result_size);
    snprintf(r.output, result_size, "[exit %d]\n%s", exit_code, buf);
    r.success = (exit_code == 0);

    free(buf);
    cJSON_Delete(args);
    return r;
}
