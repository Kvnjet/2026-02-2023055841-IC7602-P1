#ifndef HEALTH_CHECKER_SCHEDULER_H
#define HEALTH_CHECKER_SCHEDULER_H

#include "firebase_client.h"
#include "models.h"

#include <stdbool.h>
#include <signal.h>
#include <stdint.h>

typedef struct {
    void *context;
    bool (*register_checker)(void *context);
    bool (*fetch_targets)(void *context, HealthTargetList *targets);
    bool (*execute_check)(void *context, const HealthTarget *target,
                          HealthResult *result);
    bool (*publish_result)(void *context, const HealthTarget *target,
                           const HealthResult *result);
    bool (*monotonic_ms)(void *context, int64_t *milliseconds);
    void (*wait_ms)(void *context, long milliseconds);
} SchedulerOperations;

/* Testable scheduler core. It returns true after a requested shutdown and
 * false for invalid dependencies or a fatal local failure. Remote failures
 * are logged and retried without discarding the current target set. */
bool scheduler_run_with_operations(const SchedulerOperations *operations,
                                   volatile sig_atomic_t *stop_requested);

/* Runs the production scheduler using the Firebase client and protocol
 * implementations. */
bool scheduler_run(FirebaseClient *client, volatile sig_atomic_t *stop_requested);

#endif
