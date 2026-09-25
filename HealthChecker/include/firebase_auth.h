#ifndef HEALTH_CHECKER_FIREBASE_AUTH_H
#define HEALTH_CHECKER_FIREBASE_AUTH_H

#include "config.h"

#include <stdbool.h>
#include <time.h>

typedef struct {
    char *id_token;
    char *refresh_token;
    char *uid;
    time_t expires_at;
} FirebaseAuthSession;

#define FIREBASE_AUTH_SESSION_INITIALIZER {0}

/*
 * Initializes libcurl's process-wide runtime. Call this once during startup,
 * before creating worker threads. Call firebase_auth_runtime_cleanup only
 * after every component that uses libcurl has stopped.
 */
bool firebase_auth_runtime_init(void);
void firebase_auth_runtime_cleanup(void);

/* Initializes a session before its first use. */
void firebase_auth_session_init(FirebaseAuthSession *session);

/*
 * Authenticates the configured checker using Firebase email/password auth.
 * session must first be initialized with firebase_auth_session_init or
 * FIREBASE_AUTH_SESSION_INITIALIZER.
 */
bool firebase_auth_sign_in(const HealthCheckerConfig *config, FirebaseAuthSession *session);

/* Exchanges the session refresh token for a new ID token. */
bool firebase_auth_refresh(const HealthCheckerConfig *config, FirebaseAuthSession *session);

/*
 * Keeps a usable ID token in session. It refreshes shortly before expiration
 * and falls back to a fresh email/password sign-in if refresh is unavailable.
 */
bool firebase_auth_ensure_valid(const HealthCheckerConfig *config, FirebaseAuthSession *session);

bool firebase_auth_session_is_valid(const FirebaseAuthSession *session);
void firebase_auth_session_clear(FirebaseAuthSession *session);

#endif
