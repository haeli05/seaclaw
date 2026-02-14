#ifndef CCLAW_CONFIG_H
#define CCLAW_CONFIG_H

#include <stdbool.h>

typedef struct {
    /* Workspace */
    char workspace[512];

    /* Provider */
    char provider[64];       /* "anthropic" or "openai" */
    char api_key[256];
    char model[128];
    float temperature;

    /* Telegram */
    bool telegram_enabled;
    char telegram_token[256];
    char telegram_allowed[1024]; /* comma-separated user IDs */

    /* Gateway */
    int gateway_port;
    char gateway_token[128];

    /* Memory */
    char memory_db[512];     /* SQLite path */

    /* Logging */
    int log_level;           /* 0=trace .. 5=fatal */
} CClawConfig;

/* Parse config from file. Returns 0 on success. */
int config_load(CClawConfig *cfg, const char *path);

/* Load from environment variables (overrides file). */
void config_load_env(CClawConfig *cfg);

/* Set defaults. */
void config_defaults(CClawConfig *cfg);

/* Print config summary to stderr. */
void config_dump(const CClawConfig *cfg);

#endif
