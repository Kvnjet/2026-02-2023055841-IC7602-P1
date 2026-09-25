#include "firebase_client.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    HealthCheckerConfig config = {0};
    FirebaseAuthSession session = FIREBASE_AUTH_SESSION_INITIALIZER;
    FirebaseClient client;
    HealthTargetList targets;
    HealthResult result = {
        .healthy = true,
        .attempts = 3,
        .successes = 2,
        .latency_ms = 12.5,
    };
    int exit_code = 1;

    if (argc != 2 || argv[1][0] == '\0') {
        fprintf(stderr, "Usage: firebase_client_test CHECKER_UID\n");
        return 2;
    }

    health_target_list_init(&targets);

    snprintf(config.firebase_database_url,
             sizeof(config.firebase_database_url), "%s",
             "http://127.0.0.1:9000");
    snprintf(config.firebase_database_namespace,
             sizeof(config.firebase_database_namespace), "%s",
             "p1-redes-a87a7-default-rtdb");
    snprintf(config.firebase_auth_url, sizeof(config.firebase_auth_url), "%s",
             "http://127.0.0.1:9099");
    snprintf(config.firebase_api_key, sizeof(config.firebase_api_key), "%s",
             "fake-api-key");
    snprintf(config.firebase_checker_email,
             sizeof(config.firebase_checker_email), "%s",
             "checker-client-test@example.com");
    snprintf(config.firebase_checker_password,
             sizeof(config.firebase_checker_password), "%s",
             "client-test-password");
    snprintf(config.checker_id, sizeof(config.checker_id), "%s", argv[1]);
    snprintf(config.checker_city, sizeof(config.checker_city), "%s",
             "Cartago");
    snprintf(config.checker_country, sizeof(config.checker_country), "%s",
             "CR");
    config.checker_latitude = 9.8644;
    config.checker_longitude = -83.9194;

    if (!firebase_client_init(&client, &config, &session) ||
        !firebase_client_register_checker(&client) ||
        !firebase_client_fetch_targets(&client, &targets)) {
        goto cleanup;
    }
    if (targets.count != 1 ||
        strcmp(targets.items[0].record_id, "record-tcp-001") != 0 ||
        strcmp(targets.items[0].id, "target-001") != 0 ||
        targets.items[0].protocol != CHECK_TCP ||
        targets.items[0].port != 8081 ||
        targets.items[0].timeout_ms != 2000 ||
        targets.items[0].retries != 2 ||
        targets.items[0].interval_seconds != 30) {
        fprintf(stderr, "FAIL: fetched target does not match seed data.\n");
        goto cleanup;
    }

    if (!firebase_client_publish_result(&client, targets.items[0].record_id,
                                        targets.items[0].id, &result)) {
        goto cleanup;
    }

    puts("Firebase client integration tests passed.");
    exit_code = 0;

cleanup:
    health_target_list_clear(&targets);
    firebase_auth_session_clear(&session);
    firebase_auth_runtime_cleanup();
    config_clear(&config);
    return exit_code;
}
