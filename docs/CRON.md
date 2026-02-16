# Cron Scheduler

CClaw includes a built-in cron scheduler for running periodic tasks. It runs in a background thread and supports standard 5-field cron expressions.

**File:** `src/cron.c`, `src/cron.h`

## Cron Expression Format

Standard 5-field format:

```
┌──────── minute (0-59)
│ ┌────── hour (0-23)
│ │ ┌──── day of month (1-31)
│ │ │ ┌── month (1-12)
│ │ │ │ ┌ day of week (0-6, Sun=0)
│ │ │ │ │
* * * * *
```

### Supported Syntax

| Syntax | Meaning | Example |
|--------|---------|---------|
| `*` | Every value (wildcard) | `* * * * *` = every minute |
| `N` | Exact value | `30 * * * *` = at minute 30 |
| `*/N` | Every N units | `*/5 * * * *` = every 5 minutes |

**Not yet supported:** ranges (`1-5`), lists (`1,3,5`), named days/months.

## Internal Representation

```c
typedef struct {
    int minute;   // 0-59, or -1 (wildcard), or -(100+N) for */N
    int hour;     // 0-23, or -1, or -(100+N)
    int mday;     // 1-31, or -1, or -(100+N)
    int month;    // 1-12, or -1, or -(100+N)
    int wday;     // 0-6 (Sun=0), or -1, or -(100+N)
} CronExpr;
```

Step values (`*/N`) are encoded as `-(100 + N)`. For example, `*/5` for minutes is stored as `-105`. The matcher checks `time_val % N == 0`.

## Usage

### Basic Setup

```c
#include "cron.h"
#include <pthread.h>

void heartbeat(void *ud) {
    printf("Heartbeat!\n");
}

void cleanup(void *ud) {
    printf("Daily cleanup\n");
}

CronScheduler sched;
cron_init(&sched);

// Every 30 minutes
cron_add(&sched, "heartbeat", "*/30 * * * *", heartbeat, NULL);

// Daily at 3:00 AM
cron_add(&sched, "cleanup", "0 3 * * *", cleanup, NULL);

// Start in background thread
pthread_t tid;
pthread_create(&tid, NULL, (void *(*)(void *))cron_run, &sched);
```

### Stopping

```c
cron_stop(&sched);       // Signal stop
pthread_join(tid, NULL); // Wait for thread to finish (~30s max)
```

### Removing Jobs

```c
cron_remove(&sched, "heartbeat");  // Deactivates the job
```

## How It Works

1. `cron_run()` enters a loop, checking every 30 seconds
2. For each active job, it checks if the expression matches the current `localtime()`
3. Jobs only fire once per minute (tracked via `last_run` timestamp)
4. The callback runs **synchronously** in the cron thread
5. `cron_stop()` sets a flag; the sleep loop checks it every second

```c
// Simplified scheduler loop from cron.c
while (sched->running) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    for (int i = 0; i < sched->count; i++) {
        CronJob *job = &sched->jobs[i];
        if (!job->active) continue;

        time_t minute_start = now - (now % 60);
        if (job->last_run >= minute_start) continue;  // Already fired this minute

        if (cron_matches(&job->expr, tm)) {
            job->last_run = now;
            job->fn(job->userdata);
        }
    }

    // Sleep 30 seconds (interruptible)
    for (int i = 0; i < 30 && sched->running; i++)
        sleep(1);
}
```

## Limits

| Limit | Value |
|-------|-------|
| Maximum jobs | 64 (`CRON_MAX_JOBS`) |
| Job name length | 63 characters |
| Check interval | 30 seconds |
| Minimum resolution | 1 minute |
| Time zone | System local time (`localtime()`) |

## Current Usage in CClaw

In `main.c`, the cron scheduler is initialized and started in a background thread:

```c
CronScheduler cron;
cron_init(&cron);
pthread_t cron_thread;
pthread_create(&cron_thread, NULL, (void *(*)(void *))cron_run, &cron);
```

Currently no default jobs are registered — the scheduler runs empty. Jobs can be added programmatically via `cron_add()` before or after starting the scheduler.

## Thread Safety

- `cron_add()` and `cron_remove()` are **not thread-safe** with `cron_run()`. Add all jobs before starting the scheduler, or use external synchronization.
- Job callbacks run in the cron thread. If a callback needs to interact with the main thread (e.g., trigger an agent turn), use appropriate synchronization.
- `cron_stop()` is safe to call from any thread.
