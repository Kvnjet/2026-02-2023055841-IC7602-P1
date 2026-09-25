#include "firebase_auth.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    HealthCheckerConfig config = {0};
    FirebaseAuthSession session;
    int result = 1;

    if (argc != 2 || argv[1][0] == '\0') {
        fprintf(stderr, "Usage: firebase_auth_test CHECKER_UID\n");
        return 2;
    }

    firebase_auth_session_init(&session);

    snprintf(config.firebase_auth_url, sizeof(config.firebase_auth_url),
             "%s", "http://127.0.0.1:9099");
    snprintf(config.firebase_api_key, sizeof(config.firebase_api_key),
             "%s", "fake-api-key");
    snprintf(config.firebase_checker_email,
             sizeof(config.firebase_checker_email), "%s",
             "checker-auth-test@example.com");
    snprintf(config.firebase_checker_password,
             sizeof(config.firebase_checker_password), "%s",
             "auth-test-password");
    snprintf(config.checker_id, sizeof(config.checker_id), "%s", argv[1]);

    if (!firebase_auth_runtime_init()) {
        goto cleanup;
    }
    if (!firebase_auth_sign_in(&config, &session)) {
        goto cleanup;
    }
    if (!firebase_auth_session_is_valid(&session) ||
        strcmp(session.uid, config.checker_id) != 0) {
        fprintf(stderr, "FAIL: sign-in did not create a valid session.\n");
        goto cleanup;
    }

    session.expires_at = 0;
    if (!firebase_auth_ensure_valid(&config, &session)) {
        goto cleanup;
    }
    if (!firebase_auth_session_is_valid(&session) ||
        strcmp(session.uid, config.checker_id) != 0) {
        fprintf(stderr, "FAIL: refresh did not create a valid session.\n");
        goto cleanup;
    }

    snprintf(config.checker_id, sizeof(config.checker_id), "%s",
             "wrong-checker-id");
    if (firebase_auth_sign_in(&config, &session)) {
        fprintf(stderr, "FAIL: sign-in accepted a mismatched CHECKER_ID.\n");
        goto cleanup;
    }
    if (!firebase_auth_session_is_valid(&session) ||
        strcmp(session.uid, argv[1]) != 0) {
        fprintf(stderr, "FAIL: rejected sign-in replaced the valid session.\n");
        goto cleanup;
    }

    puts("Firebase Auth sign-in and refresh tests passed.");
    result = 0;

cleanup:
    firebase_auth_session_clear(&session);
    firebase_auth_runtime_cleanup();
    return result;
}
