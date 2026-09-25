#include "firebase_client.h"

#include <curl/curl.h>
#include <cjson/cJSON.h>

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATABASE_CONNECT_TIMEOUT_MS 5000L
#define DATABASE_REQUEST_TIMEOUT_MS 15000L
#define DATABASE_RESPONSE_LIMIT (8U * 1024U * 1024U)

typedef struct {
    char *data;
    size_t length;
    bool exceeded_limit;
} ResponseBuffer;

static void erase_memory(void *data, size_t length)
{
    volatile unsigned char *cursor = data;

    while (cursor != NULL && length > 0) {
        *cursor++ = 0;
        length--;
    }
}

static bool response_init(ResponseBuffer *response)
{
    response->data = malloc(1);
    if (response->data == NULL) {
        return false;
    }

    response->data[0] = '\0';
    response->length = 0;
    response->exceeded_limit = false;
    return true;
}

static void response_clear(ResponseBuffer *response)
{
    if (response == NULL) {
        return;
    }

    if (response->data != NULL) {
        erase_memory(response->data, response->length);
        free(response->data);
    }
    memset(response, 0, sizeof(*response));
}

static size_t receive_response(char *contents, size_t size, size_t count,
                               void *user_data)
{
    ResponseBuffer *response = user_data;
    size_t incoming;
    size_t new_length;
    char *resized;

    if (count != 0 && size > SIZE_MAX / count) {
        return 0;
    }
    incoming = size * count;
    if (incoming > DATABASE_RESPONSE_LIMIT ||
        response->length > DATABASE_RESPONSE_LIMIT - incoming) {
        response->exceeded_limit = true;
        return 0;
    }

    new_length = response->length + incoming;
    resized = realloc(response->data, new_length + 1);
    if (resized == NULL) {
        return 0;
    }

    response->data = resized;
    if (incoming > 0) {
        memcpy(response->data + response->length, contents, incoming);
    }
    response->data[new_length] = '\0';
    response->length = new_length;
    return incoming;
}

static bool is_valid_key(const char *key)
{
    return key != NULL && key[0] != '\0' && strpbrk(key, ".#$[]/") == NULL;
}

static char *build_database_url(CURL *curl, const FirebaseClient *client,
                                const char *path)
{
    const char *base = client->config->firebase_database_url;
    const char *query = strchr(base, '?');
    const char *base_end = query == NULL ? base + strlen(base) : query;
    const char *existing_query = query == NULL ? NULL : query + 1;
    char *escaped_token;
    char *escaped_namespace;
    char *url;
    size_t base_length;
    size_t query_length;
    size_t url_length;

    while (base_end > base && base_end[-1] == '/') {
        base_end--;
    }
    base_length = (size_t)(base_end - base);
    query_length = existing_query == NULL ? 0 : strlen(existing_query);

    escaped_token = curl_easy_escape(curl, client->auth_session->id_token, 0);
    escaped_namespace =
        curl_easy_escape(curl, client->config->firebase_database_namespace, 0);
    if (escaped_token == NULL || escaped_namespace == NULL) {
        if (escaped_token != NULL) {
            erase_memory(escaped_token, strlen(escaped_token));
        }
        curl_free(escaped_token);
        curl_free(escaped_namespace);
        return NULL;
    }

    url_length = base_length + 1 + strlen(path) + strlen(".json?") +
                 query_length + (query_length > 0 ? 1 : 0) + strlen("ns=&auth=") +
                 strlen(escaped_namespace) + strlen(escaped_token) + 1;
    url = malloc(url_length);
    if (url != NULL) {
        snprintf(url, url_length, "%.*s/%s.json?%s%sns=%s&auth=%s",
                 (int)base_length, base, path,
                 existing_query == NULL ? "" : existing_query,
                 query_length > 0 ? "&" : "", escaped_namespace,
                 escaped_token);
    }

    erase_memory(escaped_token, strlen(escaped_token));
    curl_free(escaped_token);
    curl_free(escaped_namespace);
    return url;
}

static bool configure_request(CURL *curl, const char *method, const char *url,
                              struct curl_slist *headers, const char *body,
                              ResponseBuffer *response, char *curl_error)
{
    CURLcode result;

#define SET_OPTION(option, value)                                               \
    do {                                                                        \
        result = curl_easy_setopt(curl, (option), (value));                     \
        if (result != CURLE_OK) {                                               \
            fprintf(stderr,                                                     \
                    "Firebase client error: could not configure request: %s.\n",\
                    curl_easy_strerror(result));                                \
            return false;                                                       \
        }                                                                       \
    } while (0)

    SET_OPTION(CURLOPT_URL, url);
    SET_OPTION(CURLOPT_CUSTOMREQUEST, method);
    SET_OPTION(CURLOPT_HTTPHEADER, headers);
    SET_OPTION(CURLOPT_CONNECTTIMEOUT_MS, DATABASE_CONNECT_TIMEOUT_MS);
    SET_OPTION(CURLOPT_TIMEOUT_MS, DATABASE_REQUEST_TIMEOUT_MS);
    SET_OPTION(CURLOPT_NOSIGNAL, 1L);
    SET_OPTION(CURLOPT_USERAGENT, "ic7602-health-checker/1.0");
    SET_OPTION(CURLOPT_WRITEFUNCTION, receive_response);
    SET_OPTION(CURLOPT_WRITEDATA, response);
    SET_OPTION(CURLOPT_ERRORBUFFER, curl_error);
    if (body != NULL) {
        SET_OPTION(CURLOPT_POSTFIELDS, body);
        SET_OPTION(CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)strlen(body));
    }

#undef SET_OPTION
    return true;
}

static bool request_once(FirebaseClient *client, const char *method,
                         const char *path, const char *body,
                         ResponseBuffer *response, long *status_code)
{
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    char *url = NULL;
    char curl_error[CURL_ERROR_SIZE] = {0};
    CURLcode result;
    bool success = false;

    if (!response_init(response)) {
        fprintf(stderr,
                "Firebase client error: out of memory while preparing response.\n");
        return false;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Firebase client error: could not create HTTP client.\n");
        goto cleanup;
    }

    url = build_database_url(curl, client, path);
    if (url == NULL) {
        fprintf(stderr,
                "Firebase client error: out of memory while building URL.\n");
        goto cleanup;
    }

    headers = curl_slist_append(headers, "Accept: application/json");
    if (headers == NULL) {
        fprintf(stderr,
                "Firebase client error: out of memory while building headers.\n");
        goto cleanup;
    }
    if (body != NULL) {
        struct curl_slist *updated =
            curl_slist_append(headers, "Content-Type: application/json");
        if (updated == NULL) {
            fprintf(stderr,
                    "Firebase client error: out of memory while building headers.\n");
            goto cleanup;
        }
        headers = updated;
    }

    if (!configure_request(curl, method, url, headers, body, response,
                           curl_error)) {
        goto cleanup;
    }

    result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        if (response->exceeded_limit) {
            fprintf(stderr,
                    "Firebase client error: response exceeded %u bytes.\n",
                    (unsigned int)DATABASE_RESPONSE_LIMIT);
        } else {
            fprintf(stderr, "Firebase client error: request failed: %s.\n",
                    curl_error[0] == '\0' ? curl_easy_strerror(result)
                                           : curl_error);
        }
        goto cleanup;
    }

    result = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status_code);
    if (result != CURLE_OK) {
        fprintf(stderr,
                "Firebase client error: could not read HTTP status: %s.\n",
                curl_easy_strerror(result));
        goto cleanup;
    }
    success = true;

cleanup:
    if (url != NULL) {
        erase_memory(url, strlen(url));
        free(url);
    }
    curl_slist_free_all(headers);
    if (curl != NULL) {
        curl_easy_cleanup(curl);
    }
    if (!success) {
        response_clear(response);
    }
    return success;
}

static void print_safe_message(const char *message)
{
    size_t index;

    if (message == NULL || message[0] == '\0') {
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

static void report_http_error(long status_code, const char *body)
{
    cJSON *document = cJSON_ParseWithOpts(body, NULL, true);
    const cJSON *error = NULL;
    const cJSON *message = NULL;
    const char *safe_message = NULL;

    if (document != NULL && cJSON_IsObject(document)) {
        error = cJSON_GetObjectItemCaseSensitive(document, "error");
        if (cJSON_IsString(error)) {
            safe_message = error->valuestring;
        } else if (cJSON_IsObject(error)) {
            message = cJSON_GetObjectItemCaseSensitive(error, "message");
            if (cJSON_IsString(message)) {
                safe_message = message->valuestring;
            }
        }
    }

    fprintf(stderr, "Firebase client error: HTTP %ld: ", status_code);
    print_safe_message(safe_message);
    fputs(".\n", stderr);
    cJSON_Delete(document);
}

static bool renew_authentication(FirebaseClient *client)
{
    if (client->auth_session->refresh_token != NULL &&
        client->auth_session->refresh_token[0] != '\0' &&
        firebase_auth_refresh(client->config, client->auth_session)) {
        return true;
    }
    return firebase_auth_sign_in(client->config, client->auth_session);
}

static bool authenticated_request(FirebaseClient *client, const char *method,
                                  const char *path, const char *body,
                                  ResponseBuffer *response,
                                  long *status_code)
{
    if (!firebase_auth_ensure_valid(client->config, client->auth_session)) {
        return false;
    }

    if (!request_once(client, method, path, body, response, status_code)) {
        return false;
    }
    if (*status_code != 401 && *status_code != 403) {
        return true;
    }

    response_clear(response);
    if (!renew_authentication(client)) {
        return false;
    }
    return request_once(client, method, path, body, response, status_code);
}

static bool successful_status(long status_code)
{
    return status_code >= 200 && status_code < 300;
}

static bool copy_json_string(char *destination, size_t capacity,
                             const cJSON *value, const char *field_name,
                             const char *record_id, const char *target_id)
{
    size_t length;

    if (!cJSON_IsString(value) || value->valuestring[0] == '\0') {
        fprintf(stderr,
                "Firebase client error: %s is missing for target %s/%s.\n",
                field_name, record_id, target_id);
        return false;
    }

    length = strlen(value->valuestring);
    if (length >= capacity) {
        fprintf(stderr,
                "Firebase client error: %s is too long for target %s/%s.\n",
                field_name, record_id, target_id);
        return false;
    }

    memcpy(destination, value->valuestring, length + 1);
    return true;
}

static bool parse_integer(const cJSON *value, long minimum, long maximum,
                          long *parsed)
{
    double number;
    long converted;

    if (!cJSON_IsNumber(value)) {
        return false;
    }
    number = value->valuedouble;
    if (!isfinite(number) || number < (double)minimum ||
        number > (double)maximum) {
        return false;
    }

    converted = (long)number;
    if ((double)converted != number) {
        return false;
    }
    *parsed = converted;
    return true;
}

static bool parse_http_options(const cJSON *health_check,
                               HealthTarget *target)
{
    const cJSON *path =
        cJSON_GetObjectItemCaseSensitive(health_check, "path");
    const cJSON *expected_codes =
        cJSON_GetObjectItemCaseSensitive(health_check, "expectedStatusCodes");
    const cJSON *code;
    size_t count = 0;

    if (!copy_json_string(target->http_path, sizeof(target->http_path), path,
                          "healthCheck.path", target->record_id, target->id)) {
        return false;
    }
    if (target->http_path[0] != '/') {
        fprintf(stderr,
                "Firebase client error: HTTP path must start with / for target %s/%s.\n",
                target->record_id, target->id);
        return false;
    }
    if (!cJSON_IsArray(expected_codes) || cJSON_GetArraySize(expected_codes) == 0 ||
        cJSON_GetArraySize(expected_codes) > HEALTH_MAX_EXPECTED_STATUS_CODES) {
        fprintf(stderr,
                "Firebase client error: expectedStatusCodes is invalid for target %s/%s.\n",
                target->record_id, target->id);
        return false;
    }

    cJSON_ArrayForEach(code, expected_codes) {
        long parsed;
        if (!parse_integer(code, 100, 599, &parsed)) {
            fprintf(stderr,
                    "Firebase client error: expected HTTP status is invalid for target %s/%s.\n",
                    target->record_id, target->id);
            return false;
        }
        target->expected_status_codes[count++] = (int)parsed;
    }
    target->expected_status_code_count = count;
    return true;
}

static bool parse_target(const char *record_id, const char *hostname,
                         const cJSON *target_json, HealthTarget *target)
{
    const cJSON *health_check;
    const cJSON *protocol;
    const cJSON *secret_ref;
    long parsed;

    memset(target, 0, sizeof(*target));
    if (target_json->string == NULL || !is_valid_key(target_json->string) ||
        strlen(record_id) >= sizeof(target->record_id) ||
        strlen(target_json->string) >= sizeof(target->id) ||
        strlen(hostname) >= sizeof(target->hostname)) {
        fprintf(stderr, "Firebase client error: record or target ID is invalid.\n");
        return false;
    }

    strcpy(target->record_id, record_id);
    strcpy(target->id, target_json->string);
    strcpy(target->hostname, hostname);
    if (!copy_json_string(
            target->address, sizeof(target->address),
            cJSON_GetObjectItemCaseSensitive(target_json, "address"), "address",
            target->record_id, target->id)) {
        return false;
    }
    if (!parse_integer(cJSON_GetObjectItemCaseSensitive(target_json, "port"),
                       1, 65535, &parsed)) {
        fprintf(stderr,
                "Firebase client error: port is invalid for target %s/%s.\n",
                target->record_id, target->id);
        return false;
    }
    target->port = (int)parsed;

    health_check =
        cJSON_GetObjectItemCaseSensitive(target_json, "healthCheck");
    if (!cJSON_IsObject(health_check)) {
        fprintf(stderr,
                "Firebase client error: healthCheck is missing for target %s/%s.\n",
                target->record_id, target->id);
        return false;
    }
    protocol = cJSON_GetObjectItemCaseSensitive(health_check, "protocol");
    if (!cJSON_IsString(protocol)) {
        fprintf(stderr,
                "Firebase client error: protocol is missing for target %s/%s.\n",
                target->record_id, target->id);
        return false;
    }
    if (strcmp(protocol->valuestring, "tcp") == 0) {
        target->protocol = CHECK_TCP;
    } else if (strcmp(protocol->valuestring, "http") == 0) {
        target->protocol = CHECK_HTTP;
        if (!parse_http_options(health_check, target)) {
            return false;
        }
    } else {
        fprintf(stderr,
                "Firebase client error: unsupported protocol for target %s/%s.\n",
                target->record_id, target->id);
        return false;
    }

    if (!parse_integer(cJSON_GetObjectItemCaseSensitive(health_check, "timeoutMs"),
                       1, INT_MAX, &target->timeout_ms) ||
        !parse_integer(cJSON_GetObjectItemCaseSensitive(health_check, "retries"),
                       0, INT_MAX, &parsed)) {
        fprintf(stderr,
                "Firebase client error: timeoutMs or retries is invalid for target %s/%s.\n",
                target->record_id, target->id);
        return false;
    }
    target->retries = (int)parsed;
    if (!parse_integer(
            cJSON_GetObjectItemCaseSensitive(health_check, "intervalSeconds"),
            1, INT_MAX, &parsed)) {
        fprintf(stderr,
                "Firebase client error: intervalSeconds is invalid for target %s/%s.\n",
                target->record_id, target->id);
        return false;
    }
    target->interval_seconds = (int)parsed;

    secret_ref =
        cJSON_GetObjectItemCaseSensitive(health_check, "basicAuthSecretRef");
    if (secret_ref != NULL && !cJSON_IsNull(secret_ref) &&
        !copy_json_string(target->basic_auth_secret_ref,
                          sizeof(target->basic_auth_secret_ref), secret_ref,
                          "healthCheck.basicAuthSecretRef", target->record_id,
                          target->id)) {
        return false;
    }
    return true;
}

static bool append_target(HealthTargetList *targets,
                          const HealthTarget *target)
{
    HealthTarget *resized;

    if (targets->count >= SIZE_MAX / sizeof(*targets->items)) {
        return false;
    }
    resized = realloc(targets->items,
                      (targets->count + 1) * sizeof(*targets->items));
    if (resized == NULL) {
        return false;
    }

    targets->items = resized;
    targets->items[targets->count++] = *target;
    return true;
}

static bool parse_targets(const char *json, HealthTargetList *targets)
{
    cJSON *document = cJSON_ParseWithOpts(json, NULL, true);
    const cJSON *record;
    bool success = false;

    if (document == NULL) {
        fprintf(stderr,
                "Firebase client error: dnsRecords response is invalid JSON.\n");
        return false;
    }
    if (cJSON_IsNull(document)) {
        cJSON_Delete(document);
        return true;
    }
    if (!cJSON_IsObject(document)) {
        fprintf(stderr,
                "Firebase client error: dnsRecords must be a JSON object.\n");
        goto cleanup;
    }

    cJSON_ArrayForEach(record, document) {
        const cJSON *enabled;
        const cJSON *hostname;
        const cJSON *target_json;
        const cJSON *record_targets;

        if (record->string == NULL || !is_valid_key(record->string) ||
            strlen(record->string) >= HEALTH_ID_CAPACITY ||
            !cJSON_IsObject(record)) {
            fprintf(stderr, "Firebase client error: dnsRecord is invalid.\n");
            goto cleanup;
        }

        enabled = cJSON_GetObjectItemCaseSensitive(record, "enabled");
        if (!cJSON_IsBool(enabled)) {
            fprintf(stderr,
                    "Firebase client error: enabled is missing for record %s.\n",
                    record->string);
            goto cleanup;
        }
        if (!cJSON_IsTrue(enabled)) {
            continue;
        }

        hostname = cJSON_GetObjectItemCaseSensitive(record, "hostname");
        if (!cJSON_IsString(hostname) || hostname->valuestring[0] == '\0' ||
            strlen(hostname->valuestring) >= HEALTH_HOSTNAME_CAPACITY) {
            fprintf(stderr,
                    "Firebase client error: hostname is invalid for record %s.\n",
                    record->string);
            goto cleanup;
        }
        record_targets = cJSON_GetObjectItemCaseSensitive(record, "targets");
        if (!cJSON_IsObject(record_targets)) {
            fprintf(stderr,
                    "Firebase client error: targets is invalid for record %s.\n",
                    record->string);
            goto cleanup;
        }

        cJSON_ArrayForEach(target_json, record_targets) {
            HealthTarget target;
            if (!cJSON_IsObject(target_json) ||
                !parse_target(record->string, hostname->valuestring, target_json,
                              &target)) {
                goto cleanup;
            }
            if (!append_target(targets, &target)) {
                fprintf(stderr,
                        "Firebase client error: out of memory while loading targets.\n");
                goto cleanup;
            }
        }
    }

    success = true;

cleanup:
    cJSON_Delete(document);
    if (!success) {
        health_target_list_clear(targets);
    }
    return success;
}

void health_target_list_clear(HealthTargetList *targets)
{
    if (targets == NULL) {
        return;
    }

    if (targets->items != NULL) {
        erase_memory(targets->items, targets->count * sizeof(*targets->items));
        free(targets->items);
    }
    targets->items = NULL;
    targets->count = 0;
}

void health_target_list_init(HealthTargetList *targets)
{
    if (targets != NULL) {
        memset(targets, 0, sizeof(*targets));
    }
}

bool firebase_client_init(FirebaseClient *client,
                          const HealthCheckerConfig *config,
                          FirebaseAuthSession *auth_session)
{
    if (client == NULL || config == NULL || auth_session == NULL) {
        fprintf(stderr,
                "Firebase client error: client, config, and auth session are required.\n");
        return false;
    }

    memset(client, 0, sizeof(*client));
    client->config = config;
    client->auth_session = auth_session;
    if (!firebase_auth_runtime_init() ||
        !firebase_auth_ensure_valid(config, auth_session)) {
        memset(client, 0, sizeof(*client));
        return false;
    }
    return true;
}

static cJSON *server_timestamp(void)
{
    cJSON *timestamp = cJSON_CreateObject();

    if (timestamp == NULL ||
        !cJSON_AddStringToObject(timestamp, ".sv", "timestamp")) {
        cJSON_Delete(timestamp);
        return NULL;
    }
    return timestamp;
}

static bool put_json(FirebaseClient *client, const char *path,
                     cJSON *document)
{
    ResponseBuffer response = {0};
    char *body = cJSON_PrintUnformatted(document);
    long status_code = 0;
    bool success = false;

    if (body == NULL) {
        fprintf(stderr,
                "Firebase client error: out of memory while encoding JSON.\n");
        return false;
    }

    if (!authenticated_request(client, "PUT", path, body, &response,
                               &status_code)) {
        goto cleanup;
    }
    if (!successful_status(status_code)) {
        report_http_error(status_code, response.data);
        goto cleanup;
    }
    success = true;

cleanup:
    erase_memory(body, strlen(body));
    cJSON_free(body);
    response_clear(&response);
    return success;
}

bool firebase_client_register_checker(FirebaseClient *client)
{
    cJSON *document = NULL;
    cJSON *timestamp = NULL;
    CURL *curl = NULL;
    char *checker_id = NULL;
    char *path = NULL;
    size_t path_length;
    bool success = false;

    if (client == NULL || client->config == NULL ||
        client->auth_session == NULL ||
        !is_valid_key(client->config->checker_id)) {
        fprintf(stderr, "Firebase client error: client is not initialized.\n");
        return false;
    }

    document = cJSON_CreateObject();
    timestamp = server_timestamp();
    if (document == NULL || timestamp == NULL ||
        !cJSON_AddStringToObject(document, "city",
                                client->config->checker_city) ||
        !cJSON_AddStringToObject(document, "country",
                                client->config->checker_country) ||
        !cJSON_AddNumberToObject(document, "latitude",
                                client->config->checker_latitude) ||
        !cJSON_AddNumberToObject(document, "longitude",
                                client->config->checker_longitude) ||
        !cJSON_AddItemToObject(document, "lastSeenAt", timestamp)) {
        fprintf(stderr,
                "Firebase client error: out of memory while building checker JSON.\n");
        goto cleanup;
    }
    timestamp = NULL;

    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Firebase client error: could not create HTTP client.\n");
        goto cleanup;
    }
    checker_id = curl_easy_escape(curl, client->config->checker_id, 0);
    if (checker_id == NULL) {
        fprintf(stderr,
                "Firebase client error: could not encode checker ID.\n");
        goto cleanup;
    }
    path_length = strlen("healthCheckers/") + strlen(checker_id) + 1;
    path = malloc(path_length);
    if (path == NULL) {
        fprintf(stderr,
                "Firebase client error: out of memory while building path.\n");
        goto cleanup;
    }
    snprintf(path, path_length, "healthCheckers/%s", checker_id);
    success = put_json(client, path, document);

cleanup:
    free(path);
    curl_free(checker_id);
    if (curl != NULL) {
        curl_easy_cleanup(curl);
    }
    cJSON_Delete(timestamp);
    cJSON_Delete(document);
    return success;
}

bool firebase_client_fetch_targets(FirebaseClient *client,
                                   HealthTargetList *targets)
{
    ResponseBuffer response = {0};
    long status_code = 0;
    bool success = false;

    if (client == NULL || client->config == NULL ||
        client->auth_session == NULL || targets == NULL) {
        fprintf(stderr,
                "Firebase client error: initialized client and target output are required.\n");
        return false;
    }

    health_target_list_clear(targets);
    if (!authenticated_request(client, "GET", "dnsRecords", NULL, &response,
                               &status_code)) {
        goto cleanup;
    }
    if (!successful_status(status_code)) {
        report_http_error(status_code, response.data);
        goto cleanup;
    }
    success = parse_targets(response.data, targets);

cleanup:
    response_clear(&response);
    return success;
}

bool firebase_client_publish_result(FirebaseClient *client,
                                    const char *record_id,
                                    const char *target_id,
                                    const HealthResult *result)
{
    cJSON *document = NULL;
    cJSON *timestamp = NULL;
    CURL *curl = NULL;
    char *encoded_record = NULL;
    char *encoded_target = NULL;
    char *encoded_checker = NULL;
    char *path = NULL;
    size_t path_length;
    bool majority;
    bool success = false;

    if (client == NULL || client->config == NULL ||
        client->auth_session == NULL || result == NULL ||
        !is_valid_key(record_id) || !is_valid_key(target_id) ||
        strlen(record_id) >= HEALTH_ID_CAPACITY ||
        strlen(target_id) >= HEALTH_ID_CAPACITY) {
        fprintf(stderr,
                "Firebase client error: result and valid Firebase keys are required.\n");
        return false;
    }
    if (result->attempts <= 0 || result->successes < 0 ||
        result->successes > result->attempts ||
        !isfinite(result->latency_ms) || result->latency_ms < 0.0) {
        fprintf(stderr, "Firebase client error: health result is invalid.\n");
        return false;
    }
    majority = result->successes > result->attempts / 2;
    if (result->healthy != majority) {
        fprintf(stderr,
                "Firebase client error: healthy must match the simple majority.\n");
        return false;
    }

    document = cJSON_CreateObject();
    timestamp = server_timestamp();
    if (document == NULL || timestamp == NULL ||
        !cJSON_AddBoolToObject(document, "healthy", result->healthy) ||
        !cJSON_AddNumberToObject(document, "attempts", result->attempts) ||
        !cJSON_AddNumberToObject(document, "successes", result->successes) ||
        !cJSON_AddNumberToObject(document, "latencyMs", result->latency_ms) ||
        !cJSON_AddItemToObject(document, "checkedAt", timestamp)) {
        fprintf(stderr,
                "Firebase client error: out of memory while building result JSON.\n");
        goto cleanup;
    }
    timestamp = NULL;

    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Firebase client error: could not create HTTP client.\n");
        goto cleanup;
    }
    encoded_record = curl_easy_escape(curl, record_id, 0);
    encoded_target = curl_easy_escape(curl, target_id, 0);
    encoded_checker = curl_easy_escape(curl, client->config->checker_id, 0);
    if (encoded_record == NULL || encoded_target == NULL ||
        encoded_checker == NULL) {
        fprintf(stderr,
                "Firebase client error: could not encode result path.\n");
        goto cleanup;
    }

    path_length = strlen("healthResults///") + strlen(encoded_record) +
                  strlen(encoded_target) + strlen(encoded_checker) + 1;
    path = malloc(path_length);
    if (path == NULL) {
        fprintf(stderr,
                "Firebase client error: out of memory while building path.\n");
        goto cleanup;
    }
    snprintf(path, path_length, "healthResults/%s/%s/%s", encoded_record,
             encoded_target, encoded_checker);
    success = put_json(client, path, document);

cleanup:
    free(path);
    curl_free(encoded_checker);
    curl_free(encoded_target);
    curl_free(encoded_record);
    if (curl != NULL) {
        curl_easy_cleanup(curl);
    }
    cJSON_Delete(timestamp);
    cJSON_Delete(document);
    return success;
}
