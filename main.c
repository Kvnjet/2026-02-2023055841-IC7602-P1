#include "config.h"
#include "firebase_auth.h"
#include "firebase_client.h"
#include "scheduler.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t stop_requested = 0;

static void request_shutdown(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

int main(int argc, char **argv)
{
    const char *env_path = ".env";
    HealthCheckerConfig config;
    FirebaseAuthSession auth_session = FIREBASE_AUTH_SESSION_INITIALIZER;
    FirebaseClient firebase_client;
    int exit_code = EXIT_FAILURE;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [ENV_FILE]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        env_path = argv[1];
    }

    if (!config_load(&config, env_path)) {
        return EXIT_FAILURE;
    }
    if (!firebase_client_init(&firebase_client, &config, &auth_session)) {
        goto cleanup;
    }
    if (signal(SIGINT, request_shutdown) == SIG_ERR ||
        signal(SIGTERM, request_shutdown) == SIG_ERR) {
        fprintf(stderr, "Health Checker error: could not install signal handlers.\n");
        goto cleanup;
    }

    printf("Health Checker %s starting.\n", config.checker_id);
    if (!scheduler_run(&firebase_client, &stop_requested)) {
        goto cleanup;
    }
    printf("Health Checker %s stopped.\n", config.checker_id);
    exit_code = EXIT_SUCCESS;

cleanup:
    firebase_auth_session_clear(&auth_session);
    firebase_auth_runtime_cleanup();
    config_clear(&config);
    return exit_code;
}
