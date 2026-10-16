/* Queue real requests before starting the accept workers. Startup must not
 * discard the listener, including when a worker resource cannot be created.
 * GNU linker wrapping injects failures without production test hooks. */
#define _GNU_SOURCE
#include "forge/http.h"
#include "forge/platform.h"
#include "forge/tcp.h"
#include "forge/thread.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>

enum scenario { SINGLE, AUTO, MULTI, ALLOCATION_FAILURE, FIRST_THREAD_FAILURE,
                LATER_THREAD_FAILURE, BIND_FAILURE, OPTION_FAILURE, PARTIAL_BIND_FAILURE,
                SHARED_THREAD_FAILURE };
static enum scenario scenario;
static int armed, allocation_failed, thread_calls, bind_calls, expected_port;
static int notice_fd = -1;
static int initial_server = -1;
static int fail_getsockname, failed_name_fd = -1;
static cpu_set_t expected_affinity;
static int expected_auto_workers;

static void notify(char marker) {
    ssize_t n;
    do { n = write(notice_fd, &marker, 1); } while (n < 0 && errno == EINTR);
    if (n != 1) _exit(4);
}

void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t n, size_t size) {
    if (armed && scenario == ALLOCATION_FAILURE && !allocation_failed) {
        allocation_failed = 1;
        notify('f');
        return NULL;
    }
    return __real_calloc(n, size);
}
int __real_fr_thread_start(fr_thread_t **, fr_thread_fn, void *);
int __wrap_fr_thread_start(fr_thread_t **out, fr_thread_fn fn, void *arg) {
    int call = thread_calls++;
    if (armed && ((scenario == FIRST_THREAD_FAILURE && call == 0) ||
                  ((scenario == LATER_THREAD_FAILURE || scenario == SHARED_THREAD_FAILURE) && call > 0))) {
        notify('f');
        return -1;
    }
    return __real_fr_thread_start(out, fn, arg);
}
int64_t __real_fr_tcp_listen_reuseport(int64_t);
int64_t __wrap_fr_tcp_listen_reuseport(int64_t port) {
    if (armed) assert(port == expected_port);
    int call = bind_calls++;
    if (armed && (scenario == BIND_FAILURE || scenario == SHARED_THREAD_FAILURE ||
                  (scenario == PARTIAL_BIND_FAILURE && call > 0))) {
        notify('f');
        return -1;
    }
    return __real_fr_tcp_listen_reuseport(port);
}
int __real_getsockname(int, struct sockaddr *, socklen_t *);
int __wrap_getsockname(int fd, struct sockaddr *addr, socklen_t *size) {
    if (fail_getsockname) {
        fail_getsockname = 0;
        failed_name_fd = fd;
        errno = EIO;
        return -1;
    }
    return __real_getsockname(fd, addr, size);
}
int __real_fr_sock_set_nonblocking(int);
int __wrap_fr_sock_set_nonblocking(int fd) {
    if (armed) {
        cpu_set_t actual;
        assert(sched_getaffinity(0, sizeof(actual), &actual) == 0);
        assert(CPU_EQUAL(&actual, &expected_affinity));
    }
    if (armed && fd == initial_server && gettid() == getpid() &&
        (scenario == ALLOCATION_FAILURE || scenario == FIRST_THREAD_FAILURE)) {
        /* Prove that the fault led to the caller's original-listener loop,
         * rather than an unrelated allocation/thread call failing. */
        assert(__real_fr_tcp_listen_reuseport(expected_port) < 0);
        notify('c');
    }
    return __real_fr_sock_set_nonblocking(fd);
}
int __real_setsockopt(int, int, int, const void *, socklen_t);
int __wrap_setsockopt(int fd, int level, int option, const void *value, socklen_t size) {
    if (armed && scenario == OPTION_FAILURE && fd == initial_server &&
        level == SOL_SOCKET && option == SO_REUSEPORT) {
        notify('f');
        errno = ENOPROTOOPT;
        return -1;
    }
    return __real_setsockopt(fd, level, option, value, size);
}
void __real_fr_platform_sleep_forever(void);
void __wrap_fr_platform_sleep_forever(void) {
    if (armed && scenario == AUTO) assert(thread_calls == expected_auto_workers);
    notify('r');
    __real_fr_platform_sleep_forever();
}

static const char request[] =
    "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
static const char body[] = "queued before workers";

static int send_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t n = send(fd, data, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        data += n;
        size -= (size_t)n;
    }
    return 1;
}

static int response_ok(int fd) {
    char data[2048];
    size_t used = 0;
    for (;;) {
        ssize_t n = recv(fd, data + used, sizeof(data) - 1 - used, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
            fprintf(stderr, "HTTP receive failed after %zu bytes: %s\n", used, strerror(errno));
            return 0;
        }
        used += (size_t)n;
        data[used] = '\0';
        if (!n) break;
        if (used == sizeof(data) - 1) return 0;
    }
    char *payload = strstr(data, "\r\n\r\n");
    int ok = !strncmp(data, "HTTP/1.1 200 OK\r\n", 17) && payload &&
             !strcmp(payload + 4, body);
    if (!ok) fprintf(stderr, "Unexpected HTTP response: %s\n", data);
    return ok;
}

static void queued_requests(enum scenario which, int ephemeral) {
    enum { CLIENTS = 8 };
    scenario = which;
    armed = allocation_failed = thread_calls = bind_calls = 0;
    cpu_set_t allowed;
    assert(sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
    int first = -1, last = -1;
    for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) if (CPU_ISSET(cpu, &allowed)) {
        if (first < 0) first = cpu;
        last = cpu;
    }
    assert(first >= 0);
    CPU_ZERO(&expected_affinity);
    CPU_SET(last, &expected_affinity);
    if (which == AUTO) CPU_SET(first, &expected_affinity);
#if defined(FORGE_PLATFORM_HAS_AVAILABLE_CPU_COUNT)
    expected_auto_workers = CPU_COUNT(&expected_affinity);
#else
    expected_auto_workers = fr_platform_cpu_count();
#endif
    struct sockaddr_in addr;
    int server = -1;
    /* Use an explicit port so extra workers bind the same port. Retry only
     * the temporary port reservation race, never a connection or response. */
    for (int attempt = 0; attempt < 16 && server < 0; ++attempt) {
        int reservation = (int)fr_tcp_listen(0);
        assert(reservation >= 0);
        socklen_t size = sizeof(addr);
        assert(getsockname(reservation, (struct sockaddr *)&addr, &size) == 0);
        fr_tcp_close(reservation);
        server = (int)fr_http_listen(ephemeral ? 0 : ntohs(addr.sin_port));
    }
    assert(server >= 0);
    initial_server = server;
    socklen_t size = sizeof(addr);
    assert(getsockname(server, (struct sockaddr *)&addr, &size) == 0);
    expected_port = ntohs(addr.sin_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    /* http_listen retains exclusive binding before multi-worker serving. */
    assert(__real_fr_tcp_listen_reuseport(ntohs(addr.sin_port)) < 0);
    fr_http_prepare(server, body);
    int ready[2], notice[2];
    assert(pipe(ready) == 0);
    assert(pipe(notice) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(notice[0]);
        notice_fd = notice[1];
        close(ready[1]);
        char go;
        if (read(ready[0], &go, 1) != 1) _exit(2);
        close(ready[0]);
        assert(sched_setaffinity(0, sizeof(expected_affinity), &expected_affinity) == 0);
        armed = 1;
        fr_http_serve_mt(server, which == SINGLE ? 1 : (which == AUTO ? 0 : 3));
        _exit(3);
    }
    close(notice[1]);
    close(ready[0]);
    /* The parent must release its copy: otherwise it would keep a broken
     * child's listening queue alive and conceal the original defect. */
    fr_http_server_close(server);
    int clients[CLIENTS];
    for (int i = 0; i < CLIENTS; ++i) clients[i] = -1;
    int ok = 1;
    for (int i = 0; i < CLIENTS; ++i) {
        clients[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (clients[i] < 0) { ok = 0; break; }
        struct timeval timeout = {2, 0};
        setsockopt(clients[i], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(clients[i], SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (connect(clients[i], (struct sockaddr *)&addr, sizeof(addr)) ||
            !send_all(clients[i], request, sizeof(request) - 1)) {
            ok = 0;
            break;
        }
    }
    if (write(ready[1], "g", 1) != 1) ok = 0;
    close(ready[1]);
    for (int i = 0; i < CLIENTS; ++i) {
        if (clients[i] >= 0) {
            if (ok && !response_ok(clients[i])) ok = 0;
            close(clients[i]);
        }
    }
    if (which != SINGLE && !(which == AUTO && expected_auto_workers == 1)) {
        struct pollfd event = {notice[0], POLLIN, 0};
        int failures = 0;
        for (;;) {
            char marker;
            if (poll(&event, 1, 2000) != 1 || read(notice[0], &marker, 1) != 1) {
                ok = 0;
                break;
            }
            if (marker == 'f') ++failures;
            int inline_loop = which == ALLOCATION_FAILURE || which == FIRST_THREAD_FAILURE;
            if (marker == (inline_loop ? 'c' : 'r')) break;
            if (inline_loop && marker == 'r') { ok = 0; break; }
        }
        if (which >= ALLOCATION_FAILURE && !failures) ok = 0;
    }
    close(notice[0]);
    /* A failed new worker must close its private socket; otherwise subsequent
     * flows can hash to an unserved reuseport listener. Do not retry failures. */
    for (int i = 0; ok && i < 32; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { ok = 0; break; }
        struct timeval timeout = {2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        ok = !connect(fd, (struct sockaddr *)&addr, sizeof(addr)) &&
             send_all(fd, request, sizeof(request) - 1) && response_ok(fd);
        if (!ok) fprintf(stderr, "HTTP request %d after startup failed: %s\n", i, strerror(errno));
        close(fd);
    }
    kill(child, SIGTERM);
    int status;
    assert(waitpid(child, &status, 0) == child);
    if (!ok) fprintf(stderr, "queued HTTP request lost in scenario %d\n", which);
    assert(ok);
}

int main(void) {
    fail_getsockname = 1;
    assert(fr_http_listen(0) < 0);
    assert(failed_name_fd >= 0);
    errno = 0;
    assert(fcntl(failed_name_fd, F_GETFD) < 0 && errno == EBADF);
    for (int repeat = 0; repeat < 3; ++repeat)
        for (int ephemeral = 0; ephemeral < 2; ++ephemeral)
            for (int which = SINGLE; which <= SHARED_THREAD_FAILURE; ++which)
                queued_requests((enum scenario)which, ephemeral);
    return 0;
}
