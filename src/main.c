/*
 * CClaw — OpenClaw rewritten in C
 *
 * A minimal, fast AI assistant framework.
 * Single static binary, <2MB, <50ms cold start.
 *
 * Usage:
 *   cclaw                          Interactive CLI mode
 *   cclaw "prompt"                 One-shot query
 *   cclaw --telegram               Start Telegram bot
 *   cclaw --config path/to/config  Custom config file
 *   cclaw --workspace ~/myagent    Workspace directory
 */

#include "config.h"
#include "workspace.h"
#include "http.h"
#include "provider.h"
#include "provider_openai.h"
#include "tools.h"
#include "session.h"
#include "telegram.h"
#include "memory.h"
#include "ws.h"
#include "cron.h"
#include "log.h"
#include "arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#define VERSION "0.1.0"

static volatile int g_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Print streaming text to stdout. */
static bool print_stream(const char *delta, void *ud) {
    (void)ud;
    fputs(delta, stdout);
    fflush(stdout);
    return g_running != 0;
}

/* Agent context for message handling */
typedef struct {
    CClawConfig *cfg;
    HttpClient  *http;
    char        *system_prompt;
    char        *tools_json;
} AgentCtx;

/* Run one agent turn: send message, handle tool calls, return final text. */
static char *agent_turn(AgentCtx *ctx, Session *session, const char *user_msg, bool stream) {
    session_add_user(session, user_msg);

    int max_turns = 10;
    char *final_text = NULL;

    for (int turn = 0; turn < max_turns; turn++) {
        char *msgs_json = session_messages_json(session);

        ChatResponse resp;
        bool is_openai = !strcmp(ctx->cfg->provider, "openai");

        if (is_openai) {
            if (stream) {
                resp = openai_chat_stream(ctx->http, ctx->cfg->api_key,
                                          ctx->cfg->model, ctx->system_prompt,
                                          msgs_json, ctx->tools_json,
                                          ctx->cfg->temperature,
                                          print_stream, NULL);
            } else {
                resp = openai_chat(ctx->http, ctx->cfg->api_key,
                                   ctx->cfg->model, ctx->system_prompt,
                                   msgs_json, ctx->tools_json,
                                   ctx->cfg->temperature);
            }
        } else {
            if (stream) {
                resp = provider_chat_stream(ctx->http, ctx->cfg->api_key,
                                            ctx->cfg->model, ctx->system_prompt,
                                            msgs_json, ctx->tools_json,
                                            ctx->cfg->temperature,
                                            print_stream, NULL);
            } else {
                resp = provider_chat(ctx->http, ctx->cfg->api_key,
                                     ctx->cfg->model, ctx->system_prompt,
                                     msgs_json, ctx->tools_json,
                                     ctx->cfg->temperature);
            }
        }
        free(msgs_json);

        LOG_DEBUG("API: %d in, %d out tokens, stop=%s, tools=%d",
                  resp.input_tokens, resp.output_tokens,
                  resp.stop_reason ? resp.stop_reason : "?",
                  resp.num_tools);

        /* Handle tool calls */
        if (resp.num_tools > 0) {
            /* Add assistant message with tool_use blocks */
            for (int i = 0; i < resp.num_tools; i++) {
                session_add_tool_use(session, resp.tool_calls[i].id,
                                     resp.tool_calls[i].name,
                                     resp.tool_calls[i].input_json);

                /* Execute tool */
                ToolExecResult tr = tool_execute(resp.tool_calls[i].name,
                                                 resp.tool_calls[i].input_json,
                                                 ctx->cfg->workspace);

                LOG_DEBUG("Tool %s: %s (%zu bytes)",
                          resp.tool_calls[i].name,
                          tr.success ? "ok" : "fail",
                          tr.output ? strlen(tr.output) : 0);

                session_add_tool_result(session, resp.tool_calls[i].id,
                                        tr.output ? tr.output : "");
                free(tr.output);
            }

            /* If there was also text, save it */
            if (resp.text) {
                free(final_text);
                final_text = strdup(resp.text);
            }

            chat_response_free(&resp);
            continue; /* Next turn — let LLM process tool results */
        }

        /* No tool calls — final text response */
        if (resp.text) {
            session_add_assistant(session, resp.text);
            free(final_text);
            final_text = strdup(resp.text);
        }

        chat_response_free(&resp);
        break;
    }

    session_save(session);
    return final_text;
}

/* Telegram message handler */
static char *telegram_handler(const TelegramMessage *msg, void *userdata) {
    AgentCtx *ctx = userdata;

    /* Per-chat session */
    char session_id[64];
    snprintf(session_id, sizeof(session_id), "tg_%lld", msg->chat_id);
    Session *session = session_new(ctx->cfg->workspace, session_id);

    char *reply = agent_turn(ctx, session, msg->text, false);
    session_free(session);

    return reply;
}

/* Interactive CLI mode */
static void cli_mode(AgentCtx *ctx) {
    Session *session = session_new(ctx->cfg->workspace, "cli");

    printf("CClaw v%s — type /quit to exit\n\n", VERSION);

    char input[4096];
    while (g_running) {
        printf("\033[1;36myou>\033[0m ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) break;

        /* Strip newline */
        size_t len = strlen(input);
        if (len > 0 && input[len-1] == '\n') input[--len] = '\0';
        if (len == 0) continue;

        if (!strcmp(input, "/quit") || !strcmp(input, "/exit")) break;

        printf("\033[1;33mcclaw>\033[0m ");
        fflush(stdout);

        char *reply = agent_turn(ctx, session, input, true);
        printf("\n\n");

        free(reply);
    }

    session_free(session);
}

static void print_usage(void) {
    printf("CClaw v%s — OpenClaw in C\n\n", VERSION);
    printf("Usage:\n");
    printf("  cclaw                            Interactive CLI\n");
    printf("  cclaw \"prompt\"                    One-shot query\n");
    printf("  cclaw --telegram                  Start Telegram bot\n");
    printf("  cclaw --config <file>             Config file\n");
    printf("  cclaw --workspace <dir>           Workspace directory\n");
    printf("  cclaw --model <model>             Override model\n");
    printf("  cclaw --version                   Print version\n");
}

int main(int argc, char **argv) {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    CClawConfig cfg;
    config_defaults(&cfg);

    char *config_path = NULL;
    char *one_shot = NULL;
    int telegram_mode = 0;

    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_usage();
            return 0;
        }
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            printf("cclaw %s\n", VERSION);
            return 0;
        }
        if (!strcmp(argv[i], "--config") && i + 1 < argc)
            config_path = argv[++i];
        else if (!strcmp(argv[i], "--workspace") && i + 1 < argc)
            strncpy(cfg.workspace, argv[++i], sizeof(cfg.workspace) - 1);
        else if (!strcmp(argv[i], "--model") && i + 1 < argc)
            strncpy(cfg.model, argv[++i], sizeof(cfg.model) - 1);
        else if (!strcmp(argv[i], "--telegram"))
            telegram_mode = 1;
        else if (!strcmp(argv[i], "--gateway-port") && i + 1 < argc)
            cfg.gateway_port = atoi(argv[++i]);
        else if (argv[i][0] != '-')
            one_shot = argv[i];
    }

    /* Load config */
    if (config_path) config_load(&cfg, config_path);
    else {
        /* Default config locations */
        char path[512];
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/.cclaw/config", home);
            config_load(&cfg, path);
        }
    }
    config_load_env(&cfg);

    /* Default workspace to cwd */
    if (!cfg.workspace[0]) {
        getcwd(cfg.workspace, sizeof(cfg.workspace));
    }

    log_set_level((LogLevel)cfg.log_level);

    if (!cfg.api_key[0]) {
        fprintf(stderr, "Error: no API key. Set ANTHROPIC_API_KEY or CCLAW_API_KEY.\n");
        return 1;
    }

    /* Build system prompt */
    Arena arena = arena_new(256 * 1024);
    char *system_prompt = ws_build_system_prompt(&arena, cfg.workspace, cfg.model);
    char *tools_json = tools_get_definitions();

    HttpClient *http = http_client_new();
    if (!http) {
        fprintf(stderr, "Error: failed to initialize HTTP/TLS client\n");
        return 1;
    }

    AgentCtx ctx = {
        .cfg = &cfg,
        .http = http,
        .system_prompt = system_prompt,
        .tools_json = tools_json,
    };

    config_dump(&cfg);

    /* Start cron scheduler in background thread */
    CronScheduler cron;
    cron_init(&cron);
    pthread_t cron_thread;
    bool cron_started = false;

    /* Example: heartbeat job every 30 minutes */
    /* Users can add jobs programmatically via cron_add() */
    if (1) {
        pthread_create(&cron_thread, NULL, (void *(*)(void *))cron_run, &cron);
        cron_started = true;
        LOG_INFO("Cron scheduler started in background");
    }

    /* Start WebSocket gateway if configured */
    pthread_t ws_thread;
    bool ws_started = false;

    if (cfg.gateway_port > 0) {
        typedef struct { WsServerConfig ws_cfg; AgentCtx *agent; } WsCtxWrap;

        /* WS message handler — runs agent turn for each message */
        static bool ws_on_message(int client_fd, const char *msg, size_t len, void *ud) {
            AgentCtx *actx = ud;
            (void)len;

            char session_id[64];
            snprintf(session_id, sizeof(session_id), "ws_%d", client_fd);
            Session *session = session_new(actx->cfg->workspace, session_id);

            char *reply = agent_turn(actx, session, msg, false);
            if (reply) {
                ws_send_text(client_fd, reply, strlen(reply));
                free(reply);
            }
            session_free(session);
            return true;
        }

        static WsServerConfig ws_cfg;
        ws_cfg.port = cfg.gateway_port;
        ws_cfg.auth_token = cfg.gateway_token;
        ws_cfg.on_message = ws_on_message;
        ws_cfg.userdata = &ctx;

        pthread_create(&ws_thread, NULL, (void *(*)(void *))ws_server_start, &ws_cfg);
        ws_started = true;
        LOG_INFO("WebSocket gateway starting on port %d", cfg.gateway_port);
    }

    if (telegram_mode) {
        if (!cfg.telegram_token[0]) {
            fprintf(stderr, "Error: no Telegram token. Set CCLAW_TELEGRAM_TOKEN.\n");
            return 1;
        }
        LOG_INFO("Starting Telegram bot...");
        telegram_poll_loop(http, &cfg, telegram_handler, &ctx);
    } else if (one_shot) {
        Session *session = session_new(cfg.workspace, NULL);
        char *reply = agent_turn(&ctx, session, one_shot, true);
        printf("\n");
        free(reply);
        session_free(session);
    } else {
        cli_mode(&ctx);
    }

    /* Cleanup */
    if (cron_started) {
        cron_stop(&cron);
        pthread_join(cron_thread, NULL);
    }
    if (ws_started) {
        pthread_cancel(ws_thread);
        pthread_join(ws_thread, NULL);
    }

    http_client_free(http);
    free(tools_json);
    arena_free(&arena);

    return 0;
}
