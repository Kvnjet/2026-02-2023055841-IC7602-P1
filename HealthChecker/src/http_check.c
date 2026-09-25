#include "health_check.h"

#include <curl/curl.h>

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t discard_response(char *contents, size_t size, size_t count,
                               void *user_data)
{
    (void)contents;
    (void)user_data;
    if (count != 0 && size > SIZE_MAX / count) {
        return 0;
    }
    return size * count;
}

static bool is_valid_secret_reference(const char *reference)
{
    size_t index;

    if (reference[0] == '\0') {
        return true;
    }
    if (!(isalpha((unsigned char)reference[0]) || reference[0] == '_')) {
        return false;
    }
    for (index = 1; reference[index] != '\0'; index++) {
        if (!(isalnum((unsigned char)reference[index]) ||
              reference[index] == '_')) {
            return false;
        }
    }
    return true;
}

static bool has_expected_status(const HealthTarget *target, long status_code)
{
    size_t index;

    for (index = 0; index < target->expected_status_code_count; index++) {
        if ((long)target->expected_status_codes[index] == status_code) {
            return true;
        }
    }
    return false;
}

static char *build_http_url(const HealthTarget *target)
{
    bool ipv6 = strchr(target->address, ':') != NULL;
    const char *format = ipv6 ? "http://[%s]:%d%s" : "http://%s:%d%s";
    int required = snprintf(NULL, 0, format, target->address, target->port,
                            target->http_path);
    char *url;

    if (required < 0) {
        return NULL;
    }
    url = malloc((size_t)required + 1);
    if (url != NULL) {
        snprintf(url, (size_t)required + 1, format, target->address,
                 target->port, target->http_path);
    }
    return url;
}

static bool configure_http_attempt(CURL *curl, const HealthTarget *target,
                                   const char *url, const char *credentials)
{
    CURLcode code;

#define SET_OPTION(option, value)\
    do {\
        code = curl_easy_setopt(curl, (option), (value));\
        if (code != CURLE_OK) {\
            return false;\
        }\
    } while (0)

    SET_OPTION(CURLOPT_URL, url);
    SET_OPTION(CURLOPT_CONNECTTIMEOUT_MS, target->timeout_ms);
    SET_OPTION(CURLOPT_TIMEOUT_MS, target->timeout_ms);
    SET_OPTION(CURLOPT_NOSIGNAL, 1L);
    SET_OPTION(CURLOPT_FOLLOWLOCATION, 0L);
    SET_OPTION(CURLOPT_USERAGENT, "ic7602-health-checker/1.0");
    SET_OPTION(CURLOPT_WRITEFUNCTION, discard_response);
    SET_OPTION(CURLOPT_WRITEDATA, NULL);
    if (credentials != NULL) {
        SET_OPTION(CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
        SET_OPTION(CURLOPT_USERPWD, credentials);
    }

#undef SET_OPTION
    return true;
}

static bool run_http_attempt(const HealthTarget *target, const char *url, const char *credentials, HealthCheckAttempt *attempt)
{
    CURL *curl = NULL;
    CURLcode code;
    curl_off_t elapsed_microseconds = 0;
    long status_code = 0;
    bool executed = false;

    memset(attempt, 0, sizeof(*attempt));
    curl = curl_easy_init();
    if (curl == NULL) {
        return false;
    }
    if (!configure_http_attempt(curl, target, url, credentials)) {
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME_T,
                          &elapsed_microseconds) != CURLE_OK) {
        goto cleanup;
    }
    attempt->latency_ms = (double)elapsed_microseconds / 1000.0;
    executed = true;
    if (code == CURLE_OK &&
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code) ==
            CURLE_OK) {
        attempt->succeeded = has_expected_status(target, status_code);
    }

cleanup:
    curl_easy_cleanup(curl);
    return executed;
}

bool health_check_http(const HealthTarget *target, HealthResult *result)
{
    HealthCheckAttempt *attempts = NULL;
    const char *credentials = NULL;
    char *url = NULL;
    size_t attempt_count;
    size_t index;
    bool success = false;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (target == NULL || result == NULL || target->protocol != CHECK_HTTP ||
        target->address[0] == '\0' || target->port < 1 ||
        target->port > 65535 || target->http_path[0] != '/' ||
        target->expected_status_code_count == 0 ||
        target->expected_status_code_count >
            HEALTH_MAX_EXPECTED_STATUS_CODES ||
        target->timeout_ms <= 0 || target->retries < 0 ||
        target->retries == INT_MAX ||
        !is_valid_secret_reference(target->basic_auth_secret_ref)) {
        return false;
    }

    if (target->basic_auth_secret_ref[0] != '\0') {
        credentials = getenv(target->basic_auth_secret_ref);
        if (credentials == NULL || credentials[0] == '\0' ||
            strchr(credentials, ':') == NULL) {
            fprintf(stderr,
                    "HTTP health check error: secret %s is missing or invalid.\n",
                    target->basic_auth_secret_ref);
            return false;
        }
    }

    url = build_http_url(target);
    attempt_count = (size_t)target->retries + 1;
    if (url == NULL || attempt_count > SIZE_MAX / sizeof(*attempts)) {
        goto cleanup;
    }
    attempts = calloc(attempt_count, sizeof(*attempts));
    if (attempts == NULL) {
        goto cleanup;
    }

    for (index = 0; index < attempt_count; index++) {
        if (!run_http_attempt(target, url, credentials, &attempts[index])) {
            goto cleanup;
        }
    }
    success = health_result_aggregate(attempts, attempt_count, result);

cleanup:
    free(attempts);
    free(url);
    return success;
}
