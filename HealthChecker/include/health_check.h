#ifndef HEALTH_CHECKER_HEALTH_CHECK_H
#define HEALTH_CHECKER_HEALTH_CHECK_H

#include "models.h"

#include <stdbool.h>
#include <stddef.h>

/* Result of one protocol attempt. Network failures are represented by
 * succeeded=false; they are not execution errors. */
typedef struct {
    bool succeeded;
    double latency_ms;
} HealthCheckAttempt;

/*
 * Builds a result from one or more attempts. The target is healthy only when
 * strictly more than half of the attempts succeeded. latency_ms is the mean
 * elapsed time of every attempt, including failed attempts.
 */
bool health_result_aggregate(const HealthCheckAttempt *attempts, size_t attempt_count, HealthResult *result);

/*
 * Executes retries + 1 TCP connection attempts using a nonblocking socket.
 * Returns false only for invalid input or a local execution failure. Ordinary
 * DNS, connection, and timeout failures produce a valid unhealthy result.
 */
bool health_check_tcp(const HealthTarget *target, HealthResult *result);

/* Executes retries + 1 HTTP GET attempts and validates the configured status
 * codes. If basic_auth_secret_ref is set, it names an environment variable
 * whose value must use libcurl's user:password format. */
bool health_check_http(const HealthTarget *target, HealthResult *result);

/* Dispatches a target to its configured protocol implementation. */
bool health_check_target(const HealthTarget *target, HealthResult *result);

#endif
