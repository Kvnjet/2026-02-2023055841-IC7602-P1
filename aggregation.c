#include "health_check.h"

#include <limits.h>
#include <math.h>
#include <string.h>

bool health_result_aggregate(const HealthCheckAttempt *attempts,
                             size_t attempt_count,
                             HealthResult *result)
{
    size_t index;
    int successes = 0;
    double mean_latency = 0.0;

    if (result == NULL) {
        return false;
    }
    memset(result, 0, sizeof(*result));

    if (attempts == NULL || attempt_count == 0 || attempt_count > INT_MAX) {
        return false;
    }

    for (index = 0; index < attempt_count; index++) {
        double latency = attempts[index].latency_ms;

        if (!isfinite(latency) || latency < 0.0) {
            memset(result, 0, sizeof(*result));
            return false;
        }
        if (attempts[index].succeeded) {
            successes++;
        }
        mean_latency += (latency - mean_latency) / (double)(index + 1);
    }

    result->attempts = (int)attempt_count;
    result->successes = successes;
    result->healthy = successes > (int)(attempt_count / 2);
    result->latency_ms = mean_latency;
    return true;
}

bool health_check_target(const HealthTarget *target, HealthResult *result)
{
    if (target == NULL) {
        return false;
    }
    switch (target->protocol) {
    case CHECK_TCP:
        return health_check_tcp(target, result);
    case CHECK_HTTP:
        return health_check_http(target, result);
    default:
        return false;
    }
}
