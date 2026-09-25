#ifndef HEALTH_CHECKER_FIREBASE_CLIENT_H
#define HEALTH_CHECKER_FIREBASE_CLIENT_H

#include "config.h"
#include "firebase_auth.h"
#include "models.h"

#include <stdbool.h>

typedef struct {
    const HealthCheckerConfig *config;
    FirebaseAuthSession *auth_session;
} FirebaseClient;

#define HEALTH_TARGET_LIST_INITIALIZER {0}

/*
 * Initializes the client and obtains a usable Firebase ID token.
 * auth_session must already be initialized with firebase_auth_session_init or
 * FIREBASE_AUTH_SESSION_INITIALIZER.
 */
bool firebase_client_init(FirebaseClient *client, const HealthCheckerConfig *config, FirebaseAuthSession *auth_session);

/* Writes this checker's configured location and a server-side heartbeat. */
bool firebase_client_register_checker(FirebaseClient *client);

/*
 * Loads and validates every enabled target from /dnsRecords. targets must
 * first be initialized with health_target_list_init or
 * HEALTH_TARGET_LIST_INITIALIZER.
 */
bool firebase_client_fetch_targets(FirebaseClient *client, HealthTargetList *targets);

/* Replaces this checker's current result for one record target. */
bool firebase_client_publish_result(FirebaseClient *client, const char *record_id, const char *target_id, const HealthResult *result);

void health_target_list_init(HealthTargetList *targets);
void health_target_list_clear(HealthTargetList *targets);

#endif
