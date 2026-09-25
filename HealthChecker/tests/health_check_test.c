#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "health_check.h"

#include <curl/curl.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET TestSocket;
typedef HANDLE TestThread;
#define INVALID_TEST_SOCKET INVALID_SOCKET
#define close_test_socket closesocket
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int TestSocket;
typedef pthread_t TestThread;
#define INVALID_TEST_SOCKET (-1)
#define close_test_socket close
#endif

typedef struct {
    TestSocket listener;
    const char *http_response;
    const char *expected_request_text;
    int response_delay_ms;
    bool found_expected_text;
} ServerContext;

static int failures = 0;

static void wait_milliseconds(int milliseconds)
{
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec requested;
    requested.tv_sec = milliseconds / 1000;
    requested.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&requested, NULL);
#endif
}

static void expect_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

#ifdef _WIN32
static DWORD WINAPI accept_one_connection(LPVOID argument)
#else
static void *accept_one_connection(void *argument)
#endif
{
    ServerContext *context = argument;
    TestSocket client = accept(context->listener, NULL, NULL);

    if (client != INVALID_TEST_SOCKET) {
        if (context->http_response != NULL) {
            char request[2048] = {0};
            size_t response_length = strlen(context->http_response);
            int received = recv(client, request, sizeof(request) - 1, 0);
            if (received > 0 && context->expected_request_text != NULL &&
                strstr(request, context->expected_request_text) != NULL) {
                context->found_expected_text = true;
            }
            wait_milliseconds(context->response_delay_ms);
#ifdef _WIN32
            (void)send(client, context->http_response, (int)response_length, 0);
#else
            (void)send(client, context->http_response, response_length,
                       MSG_NOSIGNAL);
#endif
        }
        close_test_socket(client);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static bool start_server(TestSocket *listener, int *port, TestThread *thread,
                         ServerContext *context, const char *http_response,
                         int response_delay_ms,
                         const char *expected_request_text)
{
    struct sockaddr_in address;
#ifdef _WIN32
    int address_length = (int)sizeof(address);
#else
    socklen_t address_length = sizeof(address);
#endif

    *listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*listener == INVALID_TEST_SOCKET) {
        return false;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(*listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(*listener, 1) != 0 ||
        getsockname(*listener, (struct sockaddr *)&address, &address_length) !=
            0) {
        close_test_socket(*listener);
        return false;
    }

    *port = ntohs(address.sin_port);
    context->listener = *listener;
    context->http_response = http_response;
    context->expected_request_text = expected_request_text;
    context->response_delay_ms = response_delay_ms;
    context->found_expected_text = false;
#ifdef _WIN32
    *thread = CreateThread(NULL, 0, accept_one_connection, context, 0, NULL);
    return *thread != NULL;
#else
    return pthread_create(thread, NULL, accept_one_connection, context) == 0;
#endif
}

static void stop_server(TestSocket listener, TestThread thread)
{
#ifdef _WIN32
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    close_test_socket(listener);
}

static void test_aggregation(void)
{
    HealthCheckAttempt attempts[] = {
        {.succeeded = true, .latency_ms = 10.0},
        {.succeeded = false, .latency_ms = 20.0},
        {.succeeded = true, .latency_ms = 30.0},
    };
    HealthCheckAttempt tied_attempts[] = {
        {.succeeded = true, .latency_ms = 4.0},
        {.succeeded = false, .latency_ms = 6.0},
    };
    HealthResult result;

    expect_true(health_result_aggregate(attempts, 3, &result),
                "aggregate valid attempts");
    expect_true(result.healthy, "two of three attempts are healthy");
    expect_true(result.attempts == 3, "aggregation records attempt count");
    expect_true(result.successes == 2, "aggregation records successes");
    expect_true(fabs(result.latency_ms - 20.0) < 0.000001,
                "aggregation computes mean latency");

    expect_true(health_result_aggregate(tied_attempts, 2, &result),
                "aggregate tied attempts");
    expect_true(!result.healthy, "one of two attempts is not a majority");
    expect_true(!health_result_aggregate(NULL, 0, &result),
                "reject empty attempts");
}

static void test_tcp_success(void)
{
    TestSocket listener;
    TestThread thread;
    ServerContext context;
    HealthTarget target = {0};
    HealthResult result;
    int port;

    if (!start_server(&listener, &port, &thread, &context, NULL, 0, NULL)) {
        expect_true(0, "start local TCP server");
        return;
    }

    strcpy(target.address, "127.0.0.1");
    target.port = port;
    target.protocol = CHECK_TCP;
    target.timeout_ms = 1000;
    target.retries = 0;
    expect_true(health_check_tcp(&target, &result),
                "execute reachable TCP check");
    expect_true(result.healthy, "reachable TCP target is healthy");
    expect_true(result.attempts == 1 && result.successes == 1,
                "reachable TCP attempt is counted");
    expect_true(result.latency_ms >= 0.0,
                "reachable TCP latency is non-negative");

    stop_server(listener, thread);
}

static void test_http_status(void)
{
    static const char response[] =
        "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    TestSocket listener;
    TestThread thread;
    ServerContext context;
    HealthTarget target = {0};
    HealthResult result;
    int port;

    if (!start_server(&listener, &port, &thread, &context, response, 0,
                      NULL)) {
        expect_true(0, "start local HTTP server");
        return;
    }

    strcpy(target.address, "127.0.0.1");
    strcpy(target.http_path, "/health");
    target.port = port;
    target.protocol = CHECK_HTTP;
    target.expected_status_codes[0] = 204;
    target.expected_status_code_count = 1;
    target.timeout_ms = 1000;
    expect_true(health_check_http(&target, &result),
                "execute HTTP status check");
    expect_true(result.healthy && result.successes == 1,
                "expected HTTP status is healthy");

    stop_server(listener, thread);
}

static void test_http_unexpected_status(void)
{
    static const char response[] =
        "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    TestSocket listener;
    TestThread thread;
    ServerContext context;
    HealthTarget target = {0};
    HealthResult result;
    int port;

    if (!start_server(&listener, &port, &thread, &context, response, 0,
                      NULL)) {
        expect_true(0, "start unexpected-status HTTP server");
        return;
    }
    strcpy(target.address, "127.0.0.1");
    strcpy(target.http_path, "/health");
    target.port = port;
    target.protocol = CHECK_HTTP;
    target.expected_status_codes[0] = 200;
    target.expected_status_code_count = 1;
    target.timeout_ms = 1000;

    expect_true(health_check_http(&target, &result),
                "execute unexpected HTTP status check");
    expect_true(!result.healthy && result.successes == 0,
                "unexpected HTTP status is unhealthy");
    stop_server(listener, thread);
}

static void test_http_timeout(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    TestSocket listener;
    TestThread thread;
    ServerContext context;
    HealthTarget target = {0};
    HealthResult result;
    int port;

    if (!start_server(&listener, &port, &thread, &context, response, 150,
                      NULL)) {
        expect_true(0, "start delayed HTTP server");
        return;
    }
    strcpy(target.address, "127.0.0.1");
    strcpy(target.http_path, "/slow");
    target.port = port;
    target.protocol = CHECK_HTTP;
    target.expected_status_codes[0] = 200;
    target.expected_status_code_count = 1;
    target.timeout_ms = 50;

    expect_true(health_check_http(&target, &result),
                "HTTP timeout produces a health result");
    expect_true(!result.healthy, "timed-out HTTP target is unhealthy");
    stop_server(listener, thread);
}

static void test_http_basic_auth(void)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    static const char expected_header[] =
        "Authorization: Basic dXNlcjpwYXNzd29yZA==";
    TestSocket listener;
    TestThread thread;
    ServerContext context;
    HealthTarget target = {0};
    HealthResult result;
    int port;

#ifdef _WIN32
    expect_true(_putenv_s("TEST_BASIC_AUTH", "user:password") == 0,
                "set basic-auth test secret");
#else
    expect_true(setenv("TEST_BASIC_AUTH", "user:password", 1) == 0,
                "set basic-auth test secret");
#endif
    if (!start_server(&listener, &port, &thread, &context, response, 0,
                      expected_header)) {
        expect_true(0, "start basic-auth HTTP server");
        return;
    }
    strcpy(target.address, "127.0.0.1");
    strcpy(target.http_path, "/private");
    strcpy(target.basic_auth_secret_ref, "TEST_BASIC_AUTH");
    target.port = port;
    target.protocol = CHECK_HTTP;
    target.expected_status_codes[0] = 200;
    target.expected_status_code_count = 1;
    target.timeout_ms = 1000;

    expect_true(health_check_http(&target, &result),
                "execute basic-auth HTTP check");
    expect_true(result.healthy, "basic-auth HTTP target is healthy");
    stop_server(listener, thread);
    expect_true(context.found_expected_text,
                "HTTP check sends credentials from referenced environment secret");
#ifdef _WIN32
    (void)_putenv_s("TEST_BASIC_AUTH", "");
#else
    unsetenv("TEST_BASIC_AUTH");
#endif
}

static void test_tcp_failure(void)
{
    HealthTarget target = {0};
    HealthResult result;

    strcpy(target.address, "invalid.invalid");
    target.port = 80;
    target.protocol = CHECK_TCP;
    target.timeout_ms = 100;
    target.retries = 2;
    expect_true(health_check_tcp(&target, &result),
                "DNS failure still produces a health result");
    expect_true(!result.healthy, "unresolvable TCP target is unhealthy");
    expect_true(result.attempts == 3 && result.successes == 0,
                "TCP retries are included in aggregation");
}

int main(void)
{
#ifdef _WIN32
    WSADATA winsock_data;
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        fprintf(stderr, "FAIL: initialize Winsock for tests\n");
        return 1;
    }
#endif
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "FAIL: initialize libcurl for tests\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    test_aggregation();
    test_tcp_success();
    test_tcp_failure();
    test_http_status();
    test_http_unexpected_status();
    test_http_timeout();
    test_http_basic_auth();

    curl_global_cleanup();

#ifdef _WIN32
    WSACleanup();
#endif
    if (failures != 0) {
        fprintf(stderr, "%d health-check test(s) failed.\n", failures);
        return 1;
    }
    puts("All health-check tests passed.");
    return 0;
}
