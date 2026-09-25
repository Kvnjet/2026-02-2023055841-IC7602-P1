#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "health_check.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET HealthSocket;
#define INVALID_HEALTH_SOCKET INVALID_SOCKET
#define close_health_socket closesocket
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int HealthSocket;
#define INVALID_HEALTH_SOCKET (-1)
#define close_health_socket close
#endif

static bool monotonic_milliseconds(double *milliseconds)
{
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;

    if (!QueryPerformanceFrequency(&frequency) ||
        !QueryPerformanceCounter(&counter) || frequency.QuadPart <= 0) {
        return false;
    }
    *milliseconds =
        (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timespec current;

    if (clock_gettime(CLOCK_MONOTONIC, &current) != 0) {
        return false;
    }
    *milliseconds = (double)current.tv_sec * 1000.0 +
                    (double)current.tv_nsec / 1000000.0;
#endif
    return true;
}

static bool set_nonblocking(HealthSocket socket_descriptor)
{
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(socket_descriptor, FIONBIO, &enabled) == 0;
#else
    int flags = fcntl(socket_descriptor, F_GETFL, 0);
    return flags >= 0 &&
           fcntl(socket_descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static bool connect_in_progress(void)
{
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ||
           error == WSAEALREADY;
#else
    return errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

static int wait_for_connection(HealthSocket socket_descriptor, int timeout_ms)
{
#ifdef _WIN32
    WSAPOLLFD descriptor = {0};
    int socket_error = 0;
    int socket_error_length = (int)sizeof(socket_error);
    int poll_result;

    descriptor.fd = socket_descriptor;
    descriptor.events = POLLWRNORM;
    poll_result = WSAPoll(&descriptor, 1, timeout_ms);
    if (poll_result <= 0 ||
        getsockopt(socket_descriptor, SOL_SOCKET, SO_ERROR,
                   (char *)&socket_error, &socket_error_length) != 0) {
        return poll_result;
    }
#else
    struct pollfd descriptor = {0};
    int socket_error = 0;
    socklen_t socket_error_length = sizeof(socket_error);
    int poll_result;

    descriptor.fd = socket_descriptor;
    descriptor.events = POLLOUT;
    do {
        poll_result = poll(&descriptor, 1, timeout_ms);
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result <= 0 ||
        getsockopt(socket_descriptor, SOL_SOCKET, SO_ERROR, &socket_error,
                   &socket_error_length) != 0) {
        return poll_result;
    }
#endif
    return socket_error == 0 ? 1 : -1;
}

static bool connect_to_any_address(const struct addrinfo *addresses,
                                   double deadline_ms)
{
    const struct addrinfo *address;

    for (address = addresses; address != NULL; address = address->ai_next) {
        HealthSocket socket_descriptor;
        double now_ms;
        double remaining_ms;
        int connect_result;
        int wait_timeout;

        if (!monotonic_milliseconds(&now_ms)) {
            return false;
        }
        remaining_ms = deadline_ms - now_ms;
        if (remaining_ms <= 0.0) {
            return false;
        }
        wait_timeout = remaining_ms >= (double)INT_MAX
                           ? INT_MAX
                           : (int)remaining_ms + 1;

        socket_descriptor =
            socket(address->ai_family, address->ai_socktype,
                   address->ai_protocol);
        if (socket_descriptor == INVALID_HEALTH_SOCKET) {
            continue;
        }
        if (!set_nonblocking(socket_descriptor)) {
            close_health_socket(socket_descriptor);
            continue;
        }

        connect_result = connect(socket_descriptor, address->ai_addr,
#ifdef _WIN32
                                 (int)address->ai_addrlen
#else
                                 address->ai_addrlen
#endif
        );
        if (connect_result == 0 ||
            (connect_in_progress() &&
             wait_for_connection(socket_descriptor, wait_timeout) > 0)) {
            close_health_socket(socket_descriptor);
            return true;
        }
        close_health_socket(socket_descriptor);
    }
    return false;
}

static bool run_tcp_attempt(const HealthTarget *target,
                            HealthCheckAttempt *attempt)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    char port[6];
    double started_ms;
    double finished_ms;
    bool resolved;

    memset(attempt, 0, sizeof(*attempt));
    if (!monotonic_milliseconds(&started_ms)) {
        return false;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    snprintf(port, sizeof(port), "%d", target->port);
    resolved = getaddrinfo(target->address, port, &hints, &addresses) == 0;
    if (resolved) {
        attempt->succeeded =
            connect_to_any_address(addresses, started_ms + target->timeout_ms);
    }

    if (addresses != NULL) {
        freeaddrinfo(addresses);
    }
    if (!monotonic_milliseconds(&finished_ms)) {
        return false;
    }
    attempt->latency_ms = finished_ms - started_ms;
    if (attempt->latency_ms < 0.0) {
        return false;
    }
    return true;
}

bool health_check_tcp(const HealthTarget *target, HealthResult *result)
{
    HealthCheckAttempt *attempts = NULL;
    size_t attempt_count;
    size_t index;
    bool success = false;
#ifdef _WIN32
    WSADATA winsock_data;
    bool winsock_started = false;
#endif

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (target == NULL || result == NULL || target->protocol != CHECK_TCP ||
        target->address[0] == '\0' || target->port < 1 ||
        target->port > 65535 || target->timeout_ms <= 0 ||
        target->retries < 0 || target->retries == INT_MAX) {
        return false;
    }

    attempt_count = (size_t)target->retries + 1;
    if (attempt_count > SIZE_MAX / sizeof(*attempts)) {
        return false;
    }
    attempts = calloc(attempt_count, sizeof(*attempts));
    if (attempts == NULL) {
        return false;
    }

#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        goto cleanup;
    }
    winsock_started = true;
#endif

    for (index = 0; index < attempt_count; index++) {
        if (!run_tcp_attempt(target, &attempts[index])) {
            goto cleanup;
        }
    }
    success = health_result_aggregate(attempts, attempt_count, result);

cleanup:
#ifdef _WIN32
    if (winsock_started) {
        WSACleanup();
    }
#endif
    free(attempts);
    return success;
}
