#include "firebase_auth.h"

#include <curl/curl.h>
#include <cjson/cJSON.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUTH_CONNECT_TIMEOUT_MS 5000L
#define AUTH_REQUEST_TIMEOUT_MS 15000L
#define AUTH_RESPONSE_LIMIT (1024U * 1024U)
#define AUTH_REFRESH_SKEW_SECONDS 60
#define AUTH_MAX_EXPIRY_SECONDS (7L * 24L * 60L * 60L)

#define IDENTITY_TOOLKIT_HOST "identitytoolkit.googleapis.com"
#define SECURE_TOKEN_HOST "securetoken.googleapis.com"
#define SIGN_IN_PATH "/v1/accounts:signInWithPassword"
#define REFRESH_PATH "/v1/token"

typedef struct {
    char *data;
    size_t length;
    bool exceeded_limit;
} ResponseBuffer;

static bool runtime_initialized = false;

static bool require_runtime(void)
{
    if (!runtime_initialized) {
        fprintf(stderr,
                "Firebase Auth error: HTTP runtime is not initialized.\n");
        return false;
    }
    return true;
}

static void secure_erase(void *data, size_t length)
{
    volatile unsigned char *cursor = data;

    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        length--;
    }
}

static void secure_free(char **value)
{
    if (value == NULL || *value == NULL) {
        return;
    }

    secure_erase(*value, strlen(*value));
    free(*value);
    *value = NULL;
}

void firebase_auth_session_init(FirebaseAuthSession *session)
{
    if (session != NULL) {
        memset(session, 0, sizeof(*session));
    }
}

void firebase_auth_session_clear(FirebaseAuthSession *session)
{
    if (session == NULL) {
        return;
    }

    secure_free(&session->id_token);
    secure_free(&session->refresh_token);
    secure_free(&session->uid);
    session->expires_at = 0;
}

bool firebase_auth_runtime_init(void)
{
    CURLcode result;

    if (runtime_initialized) {
        return true;
    }

    result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (result != CURLE_OK) {
        fprintf(stderr, "Firebase Auth error: could not initialize HTTP: %s.\n",
                curl_easy_strerror(result));
        return false;
    }

    runtime_initialized = true;
    return true;
}

void firebase_auth_runtime_cleanup(void)
{
    if (!runtime_initialized) {
        return;
    }

    curl_global_cleanup();
    runtime_initialized = false;
}

static char *duplicate_string(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        return NULL;
    }

    length = strlen(value);
    copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, value, length + 1);
    }
    return copy;
}

static bool response_buffer_init(ResponseBuffer *buffer)
{
    buffer->data = malloc(1);
    if (buffer->data == NULL) {
        return false;
    }

    buffer->data[0] = '\0';
    buffer->length = 0;
    buffer->exceeded_limit = false;
    return true;
}

static void response_buffer_clear(ResponseBuffer *buffer)
{
    if (buffer == NULL) {
        return;
    }

    if (buffer->data != NULL) {
        secure_erase(buffer->data, buffer->length);
        free(buffer->data);
    }
    memset(buffer, 0, sizeof(*buffer));
}

static size_t receive_response(char *contents, size_t size, size_t count,
                               void *user_data)
{
    ResponseBuffer *buffer = user_data;
    size_t incoming;
    size_t new_length;
    char *resized;

    if (count != 0 && size > SIZE_MAX / count) {
        return 0;
    }
    incoming = size * count;

    if (incoming > AUTH_RESPONSE_LIMIT ||
        buffer->length > AUTH_RESPONSE_LIMIT - incoming) {
        buffer->exceeded_limit = true;
        return 0;
    }

    new_length = buffer->length + incoming;
    resized = realloc(buffer->data, new_length + 1);
    if (resized == NULL) {
        return 0;
    }

    buffer->data = resized;
    if (incoming > 0) {
        memcpy(buffer->data + buffer->length, contents, incoming);
    }
    buffer->data[new_length] = '\0';
    buffer->length = new_length;
    return incoming;
}

static bool url_host_equals(const char *url, const char *expected_host)
{
    const char *scheme = strstr(url, "://");
    const char *host;
    const char *host_end;
    size_t host_length;

    if (scheme == NULL) {
        return false;
    }

    host = scheme + 3;
    host_end = strpbrk(host, ":/?#");
    host_length = host_end == NULL ? strlen(host) : (size_t)(host_end - host);
    return strlen(expected_host) == host_length &&
           strncmp(host, expected_host, host_length) == 0;
}

static char *build_endpoint(CURL *curl, const char *configured_base,
                            const char *service_host, const char *path,
                            const char *api_key)
{
    const char *scheme_end;
    const char *base_end;
    const char *separator;
    char *escaped_key;
    char *endpoint;
    size_t prefix_length;
    size_t endpoint_length;
    bool production_base;

    escaped_key = curl_easy_escape(curl, api_key, 0);
    if (escaped_key == NULL) {
        return NULL;
    }

    production_base = url_host_equals(configured_base, IDENTITY_TOOLKIT_HOST);
    if (production_base) {
        scheme_end = strstr(configured_base, "://");
        prefix_length = (size_t)(scheme_end - configured_base) + 3;
        endpoint_length = prefix_length + strlen(service_host) + strlen(path) +
                          strlen("?key=") + strlen(escaped_key) + 1;
        endpoint = malloc(endpoint_length);
        if (endpoint != NULL) {
            snprintf(endpoint, endpoint_length, "%.*s%s%s?key=%s",
                     (int)prefix_length, configured_base, service_host, path,
                     escaped_key);
        }
    } else {
        base_end = configured_base + strlen(configured_base);
        while (base_end > configured_base && base_end[-1] == '/') {
            base_end--;
        }
        prefix_length = (size_t)(base_end - configured_base);
        separator = "/";
        endpoint_length = prefix_length + strlen(separator) +
                          strlen(service_host) + strlen(path) + strlen("?key=") +
                          strlen(escaped_key) + 1;
        endpoint = malloc(endpoint_length);
        if (endpoint != NULL) {
            snprintf(endpoint, endpoint_length, "%.*s%s%s%s?key=%s",
                     (int)prefix_length, configured_base, separator,
                     service_host, path, escaped_key);
        }
    }

    curl_free(escaped_key);
    return endpoint;
}

static bool set_request_options(CURL *curl, const char *url,
                                struct curl_slist *headers, const char *body,
                                ResponseBuffer *response, char *curl_error)
{
    CURLcode result;

#define SET_OPTION(option, value)                                               \
    do {                                                                        \
        result = curl_easy_setopt(curl, (option), (value));                     \
        if (result != CURLE_OK) {                                               \
            fprintf(stderr,                                                     \
                    "Firebase Auth error: could not configure HTTP request: "   \
                    "%s.\n",                                                  \
                    curl_easy_strerror(result));                                \
            return false;                                                       \
        }                                                                       \
    } while (0)

    SET_OPTION(CURLOPT_URL, url);
    SET_OPTION(CURLOPT_HTTPHEADER, headers);
    SET_OPTION(CURLOPT_POST, 1L);
    SET_OPTION(CURLOPT_POSTFIELDS, body);
    SET_OPTION(CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)strlen(body));
    SET_OPTION(CURLOPT_CONNECTTIMEOUT_MS, AUTH_CONNECT_TIMEOUT_MS);
    SET_OPTION(CURLOPT_TIMEOUT_MS, AUTH_REQUEST_TIMEOUT_MS);
    SET_OPTION(CURLOPT_NOSIGNAL, 1L);
    SET_OPTION(CURLOPT_USERAGENT, "ic7602-health-checker/1.0");
    SET_OPTION(CURLOPT_WRITEFUNCTION, receive_response);
    SET_OPTION(CURLOPT_WRITEDATA, response);
    SET_OPTION(CURLOPT_ERRORBUFFER, curl_error);

#undef SET_OPTION
    return true;
}

static bool perform_request(const char *configured_base,
                            const char *service_host, const char *path,
                            const char *api_key, const char *content_type,
                            const char *body, ResponseBuffer *response,
                            long *status_code)
{
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    char *endpoint = NULL;
    char curl_error[CURL_ERROR_SIZE] = {0};
    CURLcode result;
    bool success = false;

    if (!require_runtime()) {
        return false;
    }
    if (!response_buffer_init(response)) {
        fprintf(stderr,
                "Firebase Auth error: out of memory while preparing response.\n");
        return false;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Firebase Auth error: could not create HTTP client.\n");
        goto cleanup;
    }

    endpoint = build_endpoint(curl, configured_base, service_host, path, api_key);
    if (endpoint == NULL) {
        fprintf(stderr,
                "Firebase Auth error: out of memory while building endpoint.\n");
        goto cleanup;
    }

    headers = curl_slist_append(headers, content_type);
    if (headers == NULL) {
        fprintf(stderr,
                "Firebase Auth error: out of memory while building headers.\n");
        goto cleanup;
    }

    if (!set_request_options(curl, endpoint, headers, body, response,
                             curl_error)) {
        goto cleanup;
    }

    result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        if (response->exceeded_limit) {
            fprintf(stderr,
                    "Firebase Auth error: server response exceeded %u bytes.\n",
                    (unsigned int)AUTH_RESPONSE_LIMIT);
        } else {
            fprintf(stderr, "Firebase Auth error: request failed: %s.\n",
                    curl_error[0] != '\0' ? curl_error
                                          : curl_easy_strerror(result));
        }
        goto cleanup;
    }

    result = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status_code);
    if (result != CURLE_OK) {
        fprintf(stderr,
                "Firebase Auth error: could not read HTTP status: %s.\n",
                curl_easy_strerror(result));
        goto cleanup;
    }

    success = true;

cleanup:
    free(endpoint);
    curl_slist_free_all(headers);
    if (curl != NULL) {
        curl_easy_cleanup(curl);
    }
    if (!success) {
        response_buffer_clear(response);
    }
    return success;
}

static void print_safe_server_message(const char *message)
{
    size_t index;

    if (message == NULL || *message == '\0') {
        fputs("request rejected", stderr);
        return;
    }

    for (index = 0; message[index] != '\0' && index < 200; index++) {
        unsigned char character = (unsigned char)message[index];
        fputc(isprint(character) ? character : '?', stderr);
    }
    if (message[index] != '\0') {
        fputs("...", stderr);
    }
}

static void report_http_error(long status_code, const char *response)
{
    const char *parse_end = NULL;
    cJSON *document = cJSON_ParseWithOpts(response, &parse_end, true);
    const cJSON *error;
    const cJSON *message = NULL;
    const char *safe_message = NULL;

    if (document != NULL) {
        error = cJSON_GetObjectItemCaseSensitive(document, "error");
        if (cJSON_IsObject(error)) {
            message = cJSON_GetObjectItemCaseSensitive(error, "message");
        }
    }
    if (message != NULL && cJSON_IsString(message)) {
        safe_message = message->valuestring;
    }

    fprintf(stderr, "Firebase Auth error: HTTP %ld: ", status_code);
    print_safe_server_message(safe_message);
    fputs(".\n", stderr);
    cJSON_Delete(document);
}

static bool require_config(const HealthCheckerConfig *config)
{
    if (config == NULL) {
        fprintf(stderr, "Firebase Auth error: config cannot be NULL.\n");
        return false;
    }

    if (config->firebase_auth_url[0] == '\0' ||
        config->firebase_api_key[0] == '\0' ||
        config->firebase_checker_email[0] == '\0' ||
        config->firebase_checker_password[0] == '\0' ||
        config->checker_id[0] == '\0') {
        fprintf(stderr,
                "Firebase Auth error: authentication configuration is incomplete.\n");
        return false;
    }
    return true;
}

static bool parse_expiry(const char *value, time_t *expires_at)
{
    char *end;
    long seconds;
    time_t now;
    time_t calculated;

    errno = 0;
    seconds = strtol(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || seconds <= 0 ||
        seconds > AUTH_MAX_EXPIRY_SECONDS) {
        return false;
    }

    now = time(NULL);
    if (now == (time_t)-1) {
        return false;
    }
    calculated = now + (time_t)seconds;
    if (calculated <= now) {
        return false;
    }

    *expires_at = calculated;
    return true;
}

static bool parse_session(const char *response, bool refresh_response,
                          const char *expected_uid,
                          FirebaseAuthSession *candidate)
{
    cJSON *document = NULL;
    const cJSON *id_token;
    const cJSON *refresh_token;
    const cJSON *uid;
    const cJSON *expires_in;
    const char *id_token_key = refresh_response ? "id_token" : "idToken";
    const char *refresh_token_key =
        refresh_response ? "refresh_token" : "refreshToken";
    const char *uid_key = refresh_response ? "user_id" : "localId";
    const char *expires_key = refresh_response ? "expires_in" : "expiresIn";
    bool success = false;

    document = cJSON_ParseWithOpts(response, NULL, true);
    if (document == NULL || !cJSON_IsObject(document)) {
        fprintf(stderr,
                "Firebase Auth error: server returned invalid JSON.\n");
        goto cleanup;
    }

    id_token = cJSON_GetObjectItemCaseSensitive(document, id_token_key);
    refresh_token = cJSON_GetObjectItemCaseSensitive(document, refresh_token_key);
    uid = cJSON_GetObjectItemCaseSensitive(document, uid_key);
    expires_in = cJSON_GetObjectItemCaseSensitive(document, expires_key);
    if (!cJSON_IsString(id_token) || id_token->valuestring[0] == '\0' ||
        !cJSON_IsString(refresh_token) || refresh_token->valuestring[0] == '\0' ||
        !cJSON_IsString(uid) || uid->valuestring[0] == '\0' ||
        !cJSON_IsString(expires_in) || expires_in->valuestring[0] == '\0') {
        fprintf(stderr,
                "Firebase Auth error: response is missing token fields.\n");
        goto cleanup;
    }

    if (strcmp(uid->valuestring, expected_uid) != 0) {
        fprintf(stderr,
                "Firebase Auth error: authenticated uid does not match CHECKER_ID.\n");
        goto cleanup;
    }

    candidate->id_token = duplicate_string(id_token->valuestring);
    candidate->refresh_token = duplicate_string(refresh_token->valuestring);
    candidate->uid = duplicate_string(uid->valuestring);
    if (candidate->id_token == NULL || candidate->refresh_token == NULL ||
        candidate->uid == NULL) {
        fprintf(stderr,
                "Firebase Auth error: out of memory while storing session.\n");
        goto cleanup;
    }
    if (!parse_expiry(expires_in->valuestring, &candidate->expires_at)) {
        fprintf(stderr,
                "Firebase Auth error: response contains an invalid expiration.\n");
        goto cleanup;
    }

    success = true;

cleanup:
    cJSON_Delete(document);
    if (!success) {
        firebase_auth_session_clear(candidate);
    }
    return success;
}

static bool replace_session_from_response(FirebaseAuthSession *session,
                                          const char *response,
                                          bool refresh_response,
                                          const char *expected_uid)
{
    FirebaseAuthSession candidate = {0};

    if (!parse_session(response, refresh_response, expected_uid, &candidate)) {
        return false;
    }

    firebase_auth_session_clear(session);
    *session = candidate;
    return true;
}

static char *create_sign_in_body(const HealthCheckerConfig *config)
{
    cJSON *document = cJSON_CreateObject();
    char *body = NULL;

    if (document == NULL ||
        !cJSON_AddStringToObject(document, "email",
                                config->firebase_checker_email) ||
        !cJSON_AddStringToObject(document, "password",
                                config->firebase_checker_password) ||
        !cJSON_AddBoolToObject(document, "returnSecureToken", true)) {
        fprintf(stderr,
                "Firebase Auth error: out of memory while building request.\n");
        cJSON_Delete(document);
        return NULL;
    }

    body = cJSON_PrintUnformatted(document);
    cJSON_Delete(document);
    if (body == NULL) {
        fprintf(stderr,
                "Firebase Auth error: out of memory while encoding request.\n");
    }
    return body;
}

bool firebase_auth_sign_in(const HealthCheckerConfig *config,
                           FirebaseAuthSession *session)
{
    ResponseBuffer response = {0};
    char *body;
    long status_code = 0;
    bool success = false;

    if (session == NULL) {
        fprintf(stderr, "Firebase Auth error: session cannot be NULL.\n");
        return false;
    }
    if (!require_config(config) || !require_runtime()) {
        return false;
    }

    body = create_sign_in_body(config);
    if (body == NULL) {
        return false;
    }

    if (!perform_request(config->firebase_auth_url, IDENTITY_TOOLKIT_HOST,
                         SIGN_IN_PATH, config->firebase_api_key,
                         "Content-Type: application/json", body, &response,
                         &status_code)) {
        goto cleanup;
    }

    if (status_code < 200 || status_code >= 300) {
        report_http_error(status_code, response.data);
        goto cleanup;
    }

    success = replace_session_from_response(session, response.data, false,
                                            config->checker_id);

cleanup:
    secure_erase(body, strlen(body));
    cJSON_free(body);
    response_buffer_clear(&response);
    return success;
}

static char *create_refresh_body(const char *refresh_token)
{
    CURL *curl;
    char *escaped_token;
    char *body;
    size_t body_length;

    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Firebase Auth error: could not create HTTP client.\n");
        return NULL;
    }

    escaped_token = curl_easy_escape(curl, refresh_token, 0);
    curl_easy_cleanup(curl);
    if (escaped_token == NULL) {
        fprintf(stderr,
                "Firebase Auth error: could not encode refresh token.\n");
        return NULL;
    }

    body_length = strlen("grant_type=refresh_token&refresh_token=") +
                  strlen(escaped_token) + 1;
    body = malloc(body_length);
    if (body != NULL) {
        snprintf(body, body_length,
                 "grant_type=refresh_token&refresh_token=%s", escaped_token);
    } else {
        fprintf(stderr,
                "Firebase Auth error: out of memory while building refresh request.\n");
    }

    curl_free(escaped_token);
    return body;
}

bool firebase_auth_refresh(const HealthCheckerConfig *config,
                           FirebaseAuthSession *session)
{
    ResponseBuffer response = {0};
    char *body;
    long status_code = 0;
    bool success = false;

    if (session == NULL) {
        fprintf(stderr, "Firebase Auth error: session cannot be NULL.\n");
        return false;
    }
    if (!require_config(config) || !require_runtime()) {
        return false;
    }
    if (session->refresh_token == NULL || session->refresh_token[0] == '\0') {
        fprintf(stderr,
                "Firebase Auth error: session does not contain a refresh token.\n");
        return false;
    }

    body = create_refresh_body(session->refresh_token);
    if (body == NULL) {
        return false;
    }

    if (!perform_request(config->firebase_auth_url, SECURE_TOKEN_HOST,
                         REFRESH_PATH, config->firebase_api_key,
                         "Content-Type: application/x-www-form-urlencoded",
                         body, &response, &status_code)) {
        goto cleanup;
    }

    if (status_code < 200 || status_code >= 300) {
        report_http_error(status_code, response.data);
        goto cleanup;
    }

    success = replace_session_from_response(session, response.data, true,
                                            config->checker_id);

cleanup:
    secure_erase(body, strlen(body));
    free(body);
    response_buffer_clear(&response);
    return success;
}

bool firebase_auth_session_is_valid(const FirebaseAuthSession *session)
{
    time_t now;

    if (session == NULL || session->id_token == NULL ||
        session->id_token[0] == '\0') {
        return false;
    }

    now = time(NULL);
    if (now == (time_t)-1 || session->expires_at <= now) {
        return false;
    }

    return session->expires_at - now > AUTH_REFRESH_SKEW_SECONDS;
}

bool firebase_auth_ensure_valid(const HealthCheckerConfig *config,
                                FirebaseAuthSession *session)
{
    if (session == NULL) {
        fprintf(stderr, "Firebase Auth error: session cannot be NULL.\n");
        return false;
    }
    if (firebase_auth_session_is_valid(session)) {
        return true;
    }

    if (session->refresh_token != NULL && session->refresh_token[0] != '\0' &&
        firebase_auth_refresh(config, session)) {
        return true;
    }

    return firebase_auth_sign_in(config, session);
}
