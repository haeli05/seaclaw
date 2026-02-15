#ifndef CCLAW_CRON_H
#define CCLAW_CRON_H

#include <stdbool.h>
#include <time.h>

#define CRON_MAX_JOBS 64

/* Cron job callback. */
typedef void (*CronJobFn)(void *userdata);

/* Cron expression (simplified: minute, hour, day-of-month, month, day-of-week).
 * -1 means wildcard (*). */
typedef struct {
    int minute;     /* 0-59 or -1 */
    int hour;       /* 0-23 or -1 */
    int mday;       /* 1-31 or -1 */
    int month;      /* 1-12 or -1 */
    int wday;       /* 0-6 (Sun=0) or -1 */
} CronExpr;

typedef struct {
    char       name[64];
    CronExpr   expr;
    CronJobFn  fn;
    void      *userdata;
    time_t     last_run;
    bool       active;
} CronJob;

typedef struct {
    CronJob jobs[CRON_MAX_JOBS];
    int     count;
    bool    running;
} CronScheduler;

/* Initialize scheduler. */
void cron_init(CronScheduler *sched);

/* Parse a cron expression string ("*/5 * * * *" style).
 * Returns 0 on success. */
int cron_parse(const char *expr_str, CronExpr *expr);

/* Add a job. Returns job index or -1 on failure. */
int cron_add(CronScheduler *sched, const char *name, const char *expr_str,
             CronJobFn fn, void *userdata);

/* Remove a job by name. */
bool cron_remove(CronScheduler *sched, const char *name);

/* Start the scheduler loop (blocking — run in a thread).
 * Checks every 30 seconds for due jobs. */
void cron_run(CronScheduler *sched);

/* Stop the scheduler. */
void cron_stop(CronScheduler *sched);

/* Check if a cron expression matches a given time. */
bool cron_matches(const CronExpr *expr, const struct tm *tm);

#endif
