#ifndef HEALTH_CHECKER_MODELS_H
#define HEALTH_CHECKER_MODELS_H

#include <stdbool.h>
#include <stddef.h>

#define HEALTH_ID_CAPACITY 128
#define HEALTH_HOSTNAME_CAPACITY 256
#define HEALTH_ADDRESS_CAPACITY 256
#define HEALTH_HTTP_PATH_CAPACITY 512
#define HEALTH_SECRET_REF_CAPACITY 128
#define HEALTH_MAX_EXPECTED_STATUS_CODES 16

typedef enum {
    CHECK_TCP,
    CHECK_HTTP
} CheckType;

typedef struct {
    char record_id[HEALTH_ID_CAPACITY];
    char id[HEALTH_ID_CAPACITY];
    char hostname[HEALTH_HOSTNAME_CAPACITY];
    char address[HEALTH_ADDRESS_CAPACITY];
    int port;
    CheckType protocol;
    char http_path[HEALTH_HTTP_PATH_CAPACITY];
    int expected_status_codes[HEALTH_MAX_EXPECTED_STATUS_CODES];
    size_t expected_status_code_count;
    char basic_auth_secret_ref[HEALTH_SECRET_REF_CAPACITY];
    long timeout_ms;
    int retries;
    int interval_seconds;
} HealthTarget;

typedef struct {
    HealthTarget *items;
    size_t count;
} HealthTargetList;

typedef struct {
    bool healthy;
    int attempts;
    int successes;
    double latency_ms;
} HealthResult;

#endif
