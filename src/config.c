#include "config.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void config_defaults(CClawConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->provider, "anthropic", sizeof(cfg->provider) - 1);
    strncpy(cfg->model, "claude-sonnet-4-20250514", sizeof(cfg->model) - 1);
    cfg->temperature = 0.7f;
    cfg->gateway_port = 3578;
    cfg->log_level = 2; /* INFO */
    strncpy(cfg->memory_db, "memory.db", sizeof(cfg->memory_db) - 1);
}

/* Trim whitespace in-place. */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/* Simple key=value config parser (TOML-ish, no sections for now). */
int config_load(CClawConfig *cfg, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        /* Skip comments and empty lines */
        char *l = trim(line);
        if (*l == '\0' || *l == '#' || *l == '[') continue;

        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = trim(l);
        char *val = trim(eq + 1);

        /* Strip quotes */
        size_t vlen = strlen(val);
        if (vlen >= 2 && val[0] == '"' && val[vlen-1] == '"') {
            val[vlen-1] = '\0';
            val++;
        }

        if      (!strcmp(key, "workspace"))        strncpy(cfg->workspace, val, sizeof(cfg->workspace)-1);
        else if (!strcmp(key, "provider"))          strncpy(cfg->provider, val, sizeof(cfg->provider)-1);
        else if (!strcmp(key, "api_key"))           strncpy(cfg->api_key, val, sizeof(cfg->api_key)-1);
        else if (!strcmp(key, "model"))             strncpy(cfg->model, val, sizeof(cfg->model)-1);
        else if (!strcmp(key, "temperature"))       cfg->temperature = (float)atof(val);
        else if (!strcmp(key, "telegram_token"))    strncpy(cfg->telegram_token, val, sizeof(cfg->telegram_token)-1);
        else if (!strcmp(key, "telegram_allowed"))  strncpy(cfg->telegram_allowed, val, sizeof(cfg->telegram_allowed)-1);
        else if (!strcmp(key, "telegram_enabled"))  cfg->telegram_enabled = (!strcmp(val, "true") || !strcmp(val, "1"));
        else if (!strcmp(key, "gateway_port"))      cfg->gateway_port = atoi(val);
        else if (!strcmp(key, "gateway_token"))     strncpy(cfg->gateway_token, val, sizeof(cfg->gateway_token)-1);
        else if (!strcmp(key, "memory_db"))         strncpy(cfg->memory_db, val, sizeof(cfg->memory_db)-1);
        else if (!strcmp(key, "log_level"))         cfg->log_level = atoi(val);
        else LOG_WARN("Unknown config key: %s", key);
    }

    fclose(fp);
    return 0;
}

void config_load_env(CClawConfig *cfg) {
    const char *v;
    if ((v = getenv("CCLAW_WORKSPACE")))    strncpy(cfg->workspace, v, sizeof(cfg->workspace)-1);
    if ((v = getenv("CCLAW_API_KEY")))       strncpy(cfg->api_key, v, sizeof(cfg->api_key)-1);
    if ((v = getenv("ANTHROPIC_API_KEY")) && !cfg->api_key[0])
        strncpy(cfg->api_key, v, sizeof(cfg->api_key)-1);
    if ((v = getenv("OPENAI_API_KEY")) && !cfg->api_key[0])
        strncpy(cfg->api_key, v, sizeof(cfg->api_key)-1);
    if ((v = getenv("CCLAW_MODEL")))         strncpy(cfg->model, v, sizeof(cfg->model)-1);
    if ((v = getenv("CCLAW_TELEGRAM_TOKEN"))) {
        strncpy(cfg->telegram_token, v, sizeof(cfg->telegram_token)-1);
        cfg->telegram_enabled = true;
    }
    if ((v = getenv("CCLAW_LOG_LEVEL")))     cfg->log_level = atoi(v);
}

void config_dump(const CClawConfig *cfg) {
    LOG_INFO("CClaw Configuration:");
    LOG_INFO("  workspace:  %s", cfg->workspace[0] ? cfg->workspace : "(cwd)");
    LOG_INFO("  provider:   %s", cfg->provider);
    LOG_INFO("  model:      %s", cfg->model);
    LOG_INFO("  api_key:    %s", cfg->api_key[0] ? "****" : "(not set)");
    LOG_INFO("  telegram:   %s", cfg->telegram_enabled ? "enabled" : "disabled");
    LOG_INFO("  gateway:    port %d", cfg->gateway_port);
    LOG_INFO("  memory_db:  %s", cfg->memory_db);
}
