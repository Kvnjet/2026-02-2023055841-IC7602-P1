#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t now_ms;
    volatile sig_atomic_t *stop_requested;
    int registrations;
    int fetches;
    int executions;
    int publications;
} FakeScheduler;

static int failures = 0;

static void expect_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static bool fake_register(void *context)
{
    FakeScheduler *fake = context;
    fake->registrations++;
    return true;
}

static bool fake_fetch(void *context, HealthTargetList *targets)
{
    FakeScheduler *fake = context;
    HealthTarget *target;

    fake->fetches++;
    health_target_list_init(targets);
    targets->items = calloc(1, sizeof(*targets->items));
    if (targets->items == NULL) {
        return false;
    }
    targets->count = 1;
    target = &targets->items[0];
    strcpy(target->record_id, "record-1");
    strcpy(target->id, "target-1");
    strcpy(target->address, "127.0.0.1");
    target->port = 8080;
    target->protocol = CHECK_TCP;
    target->timeout_ms = 100;
    target->interval_seconds = 2;
    return true;
}

static bool fake_execute(void *context, const HealthTarget *target,
                         HealthResult *result)
{
    FakeScheduler *fake = context;
    (void)target;
    fake->executions++;
    result->healthy = true;
    result->attempts = 1;
    result->successes = 1;
    result->latency_ms = 1.0;
    return true;
}

static bool fake_publish(void *context, const HealthTarget *target,
                         const HealthResult *result)
{
    FakeScheduler *fake = context;
    (void)target;
    if (!result->healthy) {
        return false;
    }
    fake->publications++;
    return true;
}

static bool fake_clock(void *context, int64_t *milliseconds)
{
    FakeScheduler *fake = context;
    *milliseconds = fake->now_ms;
    return true;
}

static void fake_wait(void *context, long milliseconds)
{
    FakeScheduler *fake = context;
    fake->now_ms += milliseconds;
    if (fake->now_ms >= 5000) {
        *fake->stop_requested = 1;
    }
}

int main(void)
{
    volatile sig_atomic_t stop_requested = 0;
    FakeScheduler fake = {.stop_requested = &stop_requested};
    SchedulerOperations operations = {
        .context = &fake,
        .register_checker = fake_register,
        .fetch_targets = fake_fetch,
        .execute_check = fake_execute,
        .publish_result = fake_publish,
        .monotonic_ms = fake_clock,
        .wait_ms = fake_wait,
    };

    expect_true(scheduler_run_with_operations(&operations, &stop_requested),
                "scheduler stops cleanly");
    expect_true(fake.registrations == 1,
                "scheduler registers checker on startup");
    expect_true(fake.fetches == 1,
                "scheduler loads targets on startup");
    expect_true(fake.executions == 3,
                "scheduler runs immediately and every configured interval");
    expect_true(fake.publications == 3,
                "scheduler publishes every completed result");

    if (failures != 0) {
        fprintf(stderr, "%d scheduler test(s) failed.\n", failures);
        return 1;
    }
    puts("All scheduler tests passed.");
    return 0;
}
