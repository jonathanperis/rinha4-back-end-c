#define _POSIX_C_SOURCE 200809L

#define main api_main
#include "../src/api/main.c"
#undef main

#include "common/fdpass.h"
#include "common/net.h"

#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

static void init_conns(conn_t conns[MAX_CONN]) {
    for (int i = 0; i < MAX_CONN; ++i) conns[i].fd = -1;
}

static int open_conn_count(conn_t conns[MAX_CONN]) {
    int count = 0;
    for (int i = 0; i < MAX_CONN; ++i) {
        if (conns[i].fd >= 0) ++count;
    }
    return count;
}

static void close_all_conns(conn_t conns[MAX_CONN]) {
    for (int i = 0; i < MAX_CONN; ++i) close_conn(&conns[i]);
}

static void prepare_passed_client_fd(int fd) {
#ifdef RINHA_ASSUME_PASSED_FD_FLAGS
    assert(rinha_set_nonblocking_cloexec(fd) == 0);
#else
    (void)fd;
#endif
}

static ssize_t read_with_timeout(int fd, char *buf, size_t len) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int r = poll(&pfd, 1, 100);
    assert(r > 0);
    assert((pfd.revents & POLLIN) != 0);
    return read(fd, buf, len);
}

static size_t count_substring(const char *haystack, const char *needle) {
    size_t count = 0;
    size_t needle_len = strlen(needle);
    const char *p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        ++count;
        p += needle_len;
    }
    return count;
}

static ssize_t read_until_responses(int fd, char *buf, size_t len, size_t expected) {
    size_t have = 0;
    while (have + 1U < len && count_substring(buf, "HTTP/1.1 200 OK") < expected) {
        ssize_t n = read_with_timeout(fd, buf + have, len - have - 1U);
        assert(n > 0);
        have += (size_t)n;
        buf[have] = '\0';
    }
    return (ssize_t)have;
}

static void test_add_conn_sets_fd_flags_by_default(void) {
#ifndef RINHA_ASSUME_PASSED_FD_FLAGS
    int client[2];
    static conn_t conns[MAX_CONN];

    init_conns(conns);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, client) == 0);

    int idx = add_conn(conns, client[0]);
    assert(idx >= 0);

    int flags = fcntl(client[0], F_GETFL, 0);
    assert(flags >= 0);
    assert((flags & O_NONBLOCK) != 0);

    int fdflags = fcntl(client[0], F_GETFD, 0);
    assert(fdflags >= 0);
    assert((fdflags & FD_CLOEXEC) != 0);

    close_all_conns(conns);
    close(client[1]);
#endif
}

static void test_passed_fd_is_processed_before_next_epoll_round(void) {
    int ctrl[2];
    int client[2];
    static conn_t conns[MAX_CONN];
    rinha_index_t index;

    memset(&index, 0, sizeof(index));
    init_conns(conns);

    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ctrl) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, client) == 0);
    assert(rinha_set_nonblocking_cloexec(ctrl[0]) == 0);
    prepare_passed_client_fd(client[0]);

    const char request[] = "GET /ready HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n";
    assert(write(client[1], request, sizeof(request) - 1) == (ssize_t)(sizeof(request) - 1));
    assert(rinha_send_fd(ctrl[1], client[0]) == 0);

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    assert(epfd >= 0);

    assert(drain_ctrl_conn(epfd, ctrl[0], conns, &index, 0) == 1);
    assert(open_conn_count(conns) == 1);

    char response[256];
    ssize_t n = read_with_timeout(client[1], response, sizeof(response) - 1);
    assert(n > 0);
    response[n] = '\0';
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "{\"status\":\"ok\"}") != NULL);

    close_all_conns(conns);
    close(epfd);
    close(client[0]);
    close(client[1]);
    close(ctrl[0]);
    close(ctrl[1]);
}

static void test_pipelined_passed_fd_requests_are_processed_immediately(void) {
    int ctrl[2];
    int client[2];
    static conn_t conns[MAX_CONN];
    rinha_index_t index;

    memset(&index, 0, sizeof(index));
    init_conns(conns);

    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ctrl) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, client) == 0);
    assert(rinha_set_nonblocking_cloexec(ctrl[0]) == 0);
    prepare_passed_client_fd(client[0]);

    const char request[] = "GET /ready HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n";
    assert(write(client[1], request, sizeof(request) - 1) == (ssize_t)(sizeof(request) - 1));
    assert(write(client[1], request, sizeof(request) - 1) == (ssize_t)(sizeof(request) - 1));
    assert(rinha_send_fd(ctrl[1], client[0]) == 0);

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    assert(epfd >= 0);

    assert(drain_ctrl_conn(epfd, ctrl[0], conns, &index, 0) == 1);
    assert(open_conn_count(conns) == 1);

    char response[512] = {0};
    ssize_t n = read_until_responses(client[1], response, sizeof(response), 2);
    assert(n > 0);
    assert(count_substring(response, "HTTP/1.1 200 OK") == 2);
    assert(count_substring(response, "{\"status\":\"ok\"}") == 2);
    assert(strstr(response, "HTTP/1.1 200 OK") < strstr(strstr(response, "HTTP/1.1 200 OK") + 1, "HTTP/1.1 200 OK"));

    close_all_conns(conns);
    close(epfd);
    close(client[0]);
    close(client[1]);
    close(ctrl[0]);
    close(ctrl[1]);
}

static void test_passed_fd_without_payload_stays_registered_on_eagain(void) {
    int ctrl[2];
    int client[2];
    static conn_t conns[MAX_CONN];
    rinha_index_t index;

    memset(&index, 0, sizeof(index));
    init_conns(conns);

    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ctrl) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, client) == 0);
    assert(rinha_set_nonblocking_cloexec(ctrl[0]) == 0);
    prepare_passed_client_fd(client[0]);
    assert(rinha_send_fd(ctrl[1], client[0]) == 0);

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    assert(epfd >= 0);

    assert(drain_ctrl_conn(epfd, ctrl[0], conns, &index, 0) == 1);
    assert(open_conn_count(conns) == 1);

    close_all_conns(conns);
    close(epfd);
    close(client[0]);
    close(client[1]);
    close(ctrl[0]);
    close(ctrl[1]);
}

int main(void) {
    test_add_conn_sets_fd_flags_by_default();
    test_passed_fd_is_processed_before_next_epoll_round();
    test_pipelined_passed_fd_requests_are_processed_immediately();
    test_passed_fd_without_payload_stays_registered_on_eagain();
    return 0;
}
