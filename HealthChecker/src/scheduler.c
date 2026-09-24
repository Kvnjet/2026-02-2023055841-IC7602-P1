#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "scheduler.h"

#include "health_check.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif

#define TARGET_REFRESH_MS 30000LL
#define CHECKER_HEARTBEAT_MS 30000LL
#define REMOTE_RETRY_MS 5000LL
#define MAX_WAIT_MS 250L

typedef struct {
    HealthTarget target;
    int64_t next_due_ms;
} ScheduledTarget;

static int64_t add_deadline(int64_t now_ms, int64_t delay_ms)
{
    if (delay_ms > INT64_MAX - now_ms) {
        return INT64_MAX;
    }
    return now_ms + delay_ms;
}

static const ScheduledTarget *find_previous_target(
    const ScheduledTarget *targets, size_t count, const HealthTarget *target)
{
    size_t index;

    for (index = 0; index < count; index++) {
        if (strcmp(targets[index].target.record_id, target->record_id) == 0 &&
            strcmp(targets[index].target.id, target->id) == 0) {
            return &targets[index];
        }
    }
    return NULL;
}

static bool replace_targets(const SchedulerOperations *operations,
                            ScheduledTarget **scheduled, size_t *count,
                            int64_t now_ms)
{
    HealthTargetList fetched = HEALTH_TARGET_LIST_INITIALIZER;
    ScheduledTarget *replacement = NULL;
    size_t index;

    if (!operations->fetch_targets(operations->context, &fetched)) {
        health_target_list_clear(&fetched);
        return false;
    }
    if (fetched.count > SIZE_MAX / sizeof(*replacement)) {
        health_target_list_clear(&fetched);
        return false;
    }
    if (fetched.count > 0) {
        replacement = calloc(fetched.count, sizeof(*replacement));
        if (replacement == NULL) {
            health_target_list_clear(&fetched);
            return false;
        }
    }

    for (index = 0; index < fetched.count; index++) {
        const ScheduledTarget *previous =
            find_previous_target(*scheduled, *count, &fetched.items[index]);
        replacement[index].target = fetched.items[index];
        replacement[index].next_due_ms =
            previous == NULL ? now_ms : previous->next_due_ms;
    }

    free(*scheduled);
    *scheduled = replacement;
    *count = fetched.count;
    health_target_list_clear(&fetched);
    printf("Scheduler loaded %zu enabled target(s).\n", *count);
    return true;
}

static int64_t target_interval_ms(const HealthTarget *target)
{
    return (int64_t)target->interval_seconds * 1000LL;
}

static void schedule_next_run(ScheduledTarget *target, int64_t now_ms)
{
    int64_t interval_ms = target_interval_ms(&target->target);

    do {
        target->next_due_ms =
            add_deadline(target->next_due_ms, interval_ms);
    } while (target->next_due_ms <= now_ms &&
             target->next_due_ms != INT64_MAX);
}

static bool run_due_targets(const SchedulerOperations *operations, ScheduledTarget *targets, size_t count, int64_t now_ms)
{
    size_t index;

    for (index = 0; index < count; index++) {
        HealthResult result;

        if (targets[index].next_due_ms > now_ms) {
            continue;
        }
        if (!operations->execute_check(operations->context,
                                       &targets[index].target, &result)) {
            fprintf(stderr,
                    "Scheduler error: could not execute target %s/%s.\n",
                    targets[index].target.record_id,
                    targets[index].target.id);
        } else {
            printf("Check %s/%s: %s (%d/%d, %.2f ms).\n",
                   targets[index].target.record_id,
                   targets[index].target.id,
                   result.healthy ? "healthy" : "unhealthy",
                   result.successes, result.attempts, result.latency_ms);
            if (!operations->publish_result(operations->context,
                                            &targets[index].target, &result)) {
                fprintf(stderr,
                        "Scheduler error: could not publish target %s/%s.\n",
                        targets[index].target.record_id,
                        targets[index].target.id);
            }
        }
        if (!operations->monotonic_ms(operations->context, &now_ms) ||
            now_ms < 0) {
            return false;
        }
        schedule_next_run(&targets[index], now_ms);
    }
    return true;
}

static int64_t next_event(int64_t refresh_ms, int64_t heartbeat_ms,
                          const ScheduledTarget *targets, size_t count)
{
    int64_t earliest = refresh_ms < heartbeat_ms ? refresh_ms : heartbeat_ms;
    size_t index;

    for (index = 0; index < count; index++) {
        if (targets[index].next_due_ms < earliest) {
            earliest = targets[index].next_due_ms;
        }
    }
    return earliest;
}

bool scheduler_run_with_operations(const SchedulerOperations *operations,
                                   volatile sig_atomic_t *stop_requested)
{
    ScheduledTarget *targets = NULL;
    size_t target_count = 0;
    int64_t now_ms;
    int64_t refresh_due_ms;
    int64_t heartbeat_due_ms;
    bool success = false;

    if (operations == NULL || stop_requested == NULL ||
        operations->register_checker == NULL ||
        operations->fetch_targets == NULL ||
        operations->execute_check == NULL ||
        operations->publish_result == NULL ||
        operations->monotonic_ms == NULL || operations->wait_ms == NULL ||
        !operations->monotonic_ms(operations->context, &now_ms) ||
        now_ms < 0) {
        return false;
    }
    if (!operations->register_checker(operations->context) ||
        !replace_targets(operations, &targets, &target_count, now_ms)) {
        goto cleanup;
    }
    refresh_due_ms = add_deadline(now_ms, TARGET_REFRESH_MS);
    heartbeat_due_ms = add_deadline(now_ms, CHECKER_HEARTBEAT_MS);

    while (!*stop_requested) {
        int64_t upcoming_ms;
        int64_t wait_ms;

        if (!operations->monotonic_ms(operations->context, &now_ms) ||
            now_ms < 0) {
            goto cleanup;
        }

        if (now_ms >= refresh_due_ms) {
            if (replace_targets(operations, &targets, &target_count, now_ms)) {
                refresh_due_ms = add_deadline(now_ms, TARGET_REFRESH_MS);
            } else {
                fprintf(stderr,
                        "Scheduler error: target refresh failed; retaining current targets.\n");
                refresh_due_ms = add_deadline(now_ms, REMOTE_RETRY_MS);
            }
        }
        if (now_ms >= heartbeat_due_ms) {
            if (!operations->register_checker(operations->context)) {
                fprintf(stderr, "Scheduler error: checker heartbeat failed.\n");
                heartbeat_due_ms = add_deadline(now_ms, REMOTE_RETRY_MS);
            } else {
                heartbeat_due_ms =
                    add_deadline(now_ms, CHECKER_HEARTBEAT_MS);
            }
        }
        if (!run_due_targets(operations, targets, target_count, now_ms)) {
            goto cleanup;
        }

        upcoming_ms = next_event(refresh_due_ms, heartbeat_due_ms, targets,
                                 target_count);
        wait_ms = upcoming_ms <= now_ms ? 1 : upcoming_ms - now_ms;
        if (wait_ms > MAX_WAIT_MS) {
            wait_ms = MAX_WAIT_MS;
        }
        operations->wait_ms(operations->context, (long)wait_ms);
    }
    success = true;

cleanup:
    free(targets);
    return success;
}

static bool production_register(void *context)
{
    return firebase_client_register_checker(context);
}

static bool production_fetch(void *context, HealthTargetList *targets)
{
    health_target_list_init(targets);
    return firebase_client_fetch_targets(context, targets);
}

static bool production_execute(void *context, const HealthTarget *target,
                               HealthResult *result)
{
    (void)context;
    return health_check_target(target, result);
}

static bool production_publish(void *context, const HealthTarget *target,
                               const HealthResult *result)
{
    return firebase_client_publish_result(context, target->record_id,
                                          target->id, result);
}

static bool production_monotonic(void *context, int64_t *milliseconds)
{
    (void)context;
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;

    if (!QueryPerformanceFrequency(&frequency) ||
        !QueryPerformanceCounter(&counter) || frequency.QuadPart <= 0) {
        return false;
    }
    *milliseconds = (int64_t)((counter.QuadPart * 1000) / frequency.QuadPart);
#else
    struct timespec current;

    if (clock_gettime(CLOCK_MONOTONIC, &current) != 0) {
        return false;
    }
    *milliseconds = (int64_t)current.tv_sec * 1000LL +
                    (int64_t)current.tv_nsec / 1000000LL;
#endif
    return true;
}

static void production_wait(void *context, long milliseconds)
{
    (void)context;
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec requested;
    struct timespec remaining;

    requested.tv_sec = milliseconds / 1000;
    requested.tv_nsec = (milliseconds % 1000) * 1000000L;
    while (nanosleep(&requested, &remaining) != 0 && errno == EINTR) {
        requested = remaining;
    }
#endif
}

bool scheduler_run(FirebaseClient *client,
                   volatile sig_atomic_t *stop_requested)
{
    SchedulerOperations operations = {
        .context = client,
        .register_checker = production_register,
        .fetch_targets = production_fetch,
        .execute_check = production_execute,
        .publish_result = production_publish,
        .monotonic_ms = production_monotonic,
        .wait_ms = production_wait,
    };

    if (client == NULL) {
        return false;
    }
    return scheduler_run_with_operations(&operations, stop_requested);
}
