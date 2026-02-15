/*
 * Built-in cron scheduler.
 *
 * Supports standard 5-field cron expressions (minute hour mday month wday).
 * Wildcards (*) and step values (*/N) are supported.
 * Runs in its own thread, checking every 30 seconds for due jobs.
 */

#include "cron.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void cron_init(CronScheduler *sched) {
    memset(sched, 0, sizeof(*sched));
}

/* Parse a single cron field. Supports: *, N, */N */
static int parse_field(const char *field, int min, int max) {
    (void)min; (void)max;
    if (!field) return -1;
    if (field[0] == '*') {
        if (field[1] == '/') {
            /* Step value — we store the step as a negative number minus 100
             * to distinguish from wildcards. E.g., */5 = -105 */
            int step = atoi(field + 2);
            if (step <= 0) return -1;
            return -(100 + step);
        }
        return -1; /* Wildcard */
    }
    return atoi(field);
}

int cron_parse(const char *expr_str, CronExpr *expr) {
    char buf[128];
    strncpy(buf, expr_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *fields[5];
    int nf = 0;
    char *tok = strtok(buf, " \t");
    while (tok && nf < 5) {
        fields[nf++] = tok;
        tok = strtok(NULL, " \t");
    }

    if (nf != 5) {
        LOG_ERROR("cron_parse: expected 5 fields, got %d", nf);
        return -1;
    }

    expr->minute = parse_field(fields[0], 0, 59);
    expr->hour   = parse_field(fields[1], 0, 23);
    expr->mday   = parse_field(fields[2], 1, 31);
    expr->month  = parse_field(fields[3], 1, 12);
    expr->wday   = parse_field(fields[4], 0, 6);

    return 0;
}

/* Check if a field matches. Handles wildcard, exact, and step. */
static bool field_matches(int field_val, int time_val) {
    if (field_val == -1) return true;  /* Wildcard */
    if (field_val < -100) {
        /* Step value: */N means time_val % N == 0 */
        int step = -(field_val + 100);
        return (time_val % step) == 0;
    }
    return field_val == time_val;
}

bool cron_matches(const CronExpr *expr, const struct tm *tm) {
    return field_matches(expr->minute, tm->tm_min)
        && field_matches(expr->hour,   tm->tm_hour)
        && field_matches(expr->mday,   tm->tm_mday)
        && field_matches(expr->month,  tm->tm_mon + 1)
        && field_matches(expr->wday,   tm->tm_wday);
}

int cron_add(CronScheduler *sched, const char *name, const char *expr_str,
             CronJobFn fn, void *userdata) {
    if (sched->count >= CRON_MAX_JOBS) {
        LOG_ERROR("cron: max jobs (%d) reached", CRON_MAX_JOBS);
        return -1;
    }

    CronJob *job = &sched->jobs[sched->count];
    strncpy(job->name, name, sizeof(job->name) - 1);

    if (cron_parse(expr_str, &job->expr) != 0) {
        LOG_ERROR("cron: invalid expression '%s' for job '%s'", expr_str, name);
        return -1;
    }

    job->fn = fn;
    job->userdata = userdata;
    job->last_run = 0;
    job->active = true;

    LOG_INFO("cron: added job '%s' [%s]", name, expr_str);
    return sched->count++;
}

bool cron_remove(CronScheduler *sched, const char *name) {
    for (int i = 0; i < sched->count; i++) {
        if (!strcmp(sched->jobs[i].name, name)) {
            sched->jobs[i].active = false;
            LOG_INFO("cron: removed job '%s'", name);
            return true;
        }
    }
    return false;
}

void cron_run(CronScheduler *sched) {
    sched->running = true;
    LOG_INFO("cron: scheduler started (%d jobs)", sched->count);

    while (sched->running) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);

        for (int i = 0; i < sched->count; i++) {
            CronJob *job = &sched->jobs[i];
            if (!job->active) continue;

            /* Only fire once per minute */
            time_t minute_start = now - (now % 60);
            if (job->last_run >= minute_start) continue;

            if (cron_matches(&job->expr, tm)) {
                LOG_DEBUG("cron: firing job '%s'", job->name);
                job->last_run = now;
                job->fn(job->userdata);
            }
        }

        /* Sleep 30 seconds between checks */
        for (int i = 0; i < 30 && sched->running; i++) {
            sleep(1);
        }
    }

    LOG_INFO("cron: scheduler stopped");
}

void cron_stop(CronScheduler *sched) {
    sched->running = false;
}
