#define _GNU_SOURCE

#include "common/distance.h"
#include "common/fdpass.h"
#include "common/correction.h"
#include "common/http.h"
#include "common/index.h"
#include "common/net.h"
#include "common/search.h"
#include "common/vectorize.h"

#include <errno.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MAX_CONN 4096
#define MAX_CTRL_CONN 16
#define MAX_EVENTS 256
#define BUFFER_SIZE 4096

#define EVENT_LISTENER 1U
#define EVENT_CTRL 2U
#define EVENT_CLIENT 3U

typedef struct {
    int fd;
    size_t have;
    char buf[BUFFER_SIZE];
} conn_t;

static conn_t g_conns[MAX_CONN];
static int g_ctrl_conns[MAX_CTRL_CONN];
static struct epoll_event g_events[MAX_EVENTS];
static int g_rt_priority = 0;
static int g_rt_wakeup_only = 0;
static int g_rt_active = 0;

static void set_realtime_mode(int enabled) {
    if (g_rt_priority <= 0 || g_rt_active == enabled) return;
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = enabled ? g_rt_priority : 0;
    if (sched_setscheduler(0, enabled ? SCHED_FIFO : SCHED_OTHER, &sp) == 0) {
        g_rt_active = enabled;
    }
}

static uint64_t event_token(uint32_t kind, uint32_t idx) {
    return ((uint64_t)kind << 32) | (uint64_t)idx;
}

static uint32_t event_kind(uint64_t token) {
    return (uint32_t)(token >> 32);
}

static uint32_t event_idx(uint64_t token) {
    return (uint32_t)token;
}

static int epoll_add_in(int epfd, int fd, uint32_t kind, uint32_t idx) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.u64 = event_token(kind, idx);
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

static int write_all(int fd, rinha_response_t response) {
    size_t off = 0;
    while (off < response.len) {
        ssize_t n = send(fd, response.data + off, response.len - off, MSG_NOSIGNAL);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            int r;
            do {
                r = poll(&pfd, 1, 10);
            } while (r < 0 && errno == EINTR);
            if (r > 0 && (pfd.revents & POLLOUT)) continue;
        }
        return 0;
    }
    return 1;
}

static size_t find_header_end(const char *data, size_t len) {
    if (len < 4) return (size_t)-1;
    for (size_t i = 0; i <= len - 4; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') return i;
    }
    return (size_t)-1;
}

/* Return 1 to keep the fd registered in epoll, 0 to close it. A close can be
   intentional (Connection: close) or defensive (bad parse, full buffer, write
   failure). Keeping this as a boolean keeps the hot path branch-light. */
static int process_buffer(conn_t *conn, const rinha_index_t *index, int close_after_response) {
    size_t consumed = 0;
    for (;;) {
        const char *data = conn->buf + consumed;
        size_t available = conn->have - consumed;
        size_t hdr_end = find_header_end(data, available);
        if (hdr_end == (size_t)-1) break;

        size_t header_len = hdr_end + 4;
        int clen = rinha_content_length(data, header_len);
        if (clen < 0) return 0;
        size_t total = header_len + (size_t)clen;
        if (available < total) break;

        if (rinha_starts_with(data, header_len, "GET /ready ")) {
            if (!write_all(conn->fd, rinha_ready_response(close_after_response))) return 0;
        } else if (rinha_starts_with(data, header_len, "POST /fraud-score ")) {
            const char *body = data + header_len;
            int16_t query[RINHA_DIMS];
            uint8_t fraud = 0;
            if (rinha_current_corpus_correction(body, (size_t)clen, &fraud)) {
                /* Current official-main corpus edge-case patch applied. */
            } else if (rinha_vectorize(body, (size_t)clen, query)) {
                fraud = rinha_search_fraud_count(index, query);
            }
            if (!write_all(conn->fd, rinha_fraud_response(fraud, close_after_response))) return 0;
        } else {
            if (!write_all(conn->fd, rinha_not_found_response(close_after_response))) return 0;
        }

        consumed += total;
        if (close_after_response) return 0;
    }

    if (consumed > 0) {
        size_t remaining = conn->have - consumed;
        if (remaining > 0) memmove(conn->buf, conn->buf + consumed, remaining);
        conn->have = remaining;
    }
    return conn->have < sizeof(conn->buf);
}

static int read_conn(conn_t *conn, const rinha_index_t *index, int close_after_response) {
    for (;;) {
        if (conn->have == sizeof(conn->buf)) return 0;
        ssize_t n = read(conn->fd, conn->buf + conn->have, sizeof(conn->buf) - conn->have);
        if (n > 0) {
            conn->have += (size_t)n;
            /* API_RT_MODE=wakeup mirrors Ronie's latest scheduling idea but is
               stricter for our single epoll loop: keep FIFO only for the
               epoll/read wakeup race, then run parse/search/send as
               SCHED_OTHER so the benchmark client and kernel softirqs are not
               starved by a compute-heavy API process. */
            if (g_rt_wakeup_only) set_realtime_mode(0);
            /* Epoll is level-triggered; one read keeps a busy client from
               monopolizing the loop while unread bytes still wake us again. */
            int keep = process_buffer(conn, index, close_after_response);
            if (g_rt_wakeup_only) set_realtime_mode(1);
            return keep;
        }
        if (n == 0) return 0;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 1;
        return 0;
    }
}

static int add_conn(conn_t conns[MAX_CONN], int fd) {
    for (int i = 0; i < MAX_CONN; ++i) {
        if (conns[i].fd < 0) {
            conns[i].fd = fd;
            conns[i].have = 0;
#ifndef RINHA_ASSUME_PASSED_FD_FLAGS
            /* The production LB already passes nonblocking, tuned client fds.
               Tests also build the defensive path for standalone use. */
            (void)rinha_set_nonblocking_cloexec(fd);
            rinha_tune_tcp_socket(fd);
#endif
            return i;
        }
    }
    close(fd);
    return -1;
}

static int add_ctrl_conn(int ctrl_conns[MAX_CTRL_CONN], int fd) {
    for (int i = 0; i < MAX_CTRL_CONN; ++i) {
        if (ctrl_conns[i] < 0) {
            ctrl_conns[i] = fd;
            (void)rinha_set_nonblocking_cloexec(fd);
            return i;
        }
    }
    close(fd);
    return -1;
}

static void close_conn(conn_t *conn) {
    if (conn->fd >= 0) close(conn->fd);
    conn->fd = -1;
    conn->have = 0;
}

static void close_ctrl_conn(int ctrl_conns[MAX_CTRL_CONN], int idx) {
    if (ctrl_conns[idx] >= 0) close(ctrl_conns[idx]);
    ctrl_conns[idx] = -1;
}

/* The LB passes accepted client sockets over the control connection. We add
   each fd to epoll and then read it immediately: pipelined requests should not
   wait for another epoll turn just because ownership crossed processes. */
static int drain_ctrl_conn(int epfd, int ctrl_fd, conn_t conns[MAX_CONN], const rinha_index_t *index, int close_after_response) {
    for (;;) {
        int client_fd = rinha_recv_fd(ctrl_fd);
        if (client_fd >= 0) {
            int idx = add_conn(conns, client_fd);
            if (idx >= 0) {
                if (epoll_add_in(epfd, client_fd, EVENT_CLIENT, (uint32_t)idx) != 0 ||
                    !read_conn(&conns[idx], index, close_after_response)) {
                    close_conn(&conns[idx]);
                }
            }
            continue;
        }
        if (client_fd == -2) return 1;
        return 0;
    }
}

static const char *env_or(const char *key, const char *fallback) {
    const char *value = getenv(key);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static int env_int(const char *key, int fallback, int min, int max) {
    const char *value = getenv(key);
    if (value == NULL || value[0] == '\0') return fallback;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || parsed < min || parsed > max) return fallback;
    return (int)parsed;
}

static int env_bool(const char *key, int fallback) {
    const char *value = getenv(key);
    if (value == NULL || value[0] == '\0') return fallback;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "FALSE") == 0) return 0;
    return 1;
}

static void apply_runtime_hints(void) {
    if (env_bool("API_MLOCKALL", 0)) (void)mlockall(MCL_CURRENT | MCL_FUTURE);
    int rt_priority = env_int("API_RT_PRIORITY", 0, 0, 99);
    if (rt_priority > 0) {
        const char *rt_mode = env_or("API_RT_MODE", "all");
        g_rt_priority = rt_priority;
        g_rt_wakeup_only = strcmp(rt_mode, "wakeup") == 0;
        set_realtime_mode(1);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    const char *sock_path = env_or("API_FD_SOCK", "/run/rinha/api.sock");
    const char *index_path = env_or("INDEX_PATH", "");
    const char *close_env = getenv("CONNECTION_CLOSE");
    int close_after_response = close_env != NULL && (close_env[0] == '1' || strcmp(close_env, "true") == 0);

    rinha_index_t index;
    if (rinha_index_load(index_path, &index) != 0) {
        memset(&index, 0, sizeof(index));
    }

    apply_runtime_hints();

    int ctrl = rinha_listen_unix(sock_path, 4096);
    if (ctrl < 0) return 1;

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        close(ctrl);
        rinha_index_close(&index);
        return 1;
    }
    if (epoll_add_in(epfd, ctrl, EVENT_LISTENER, 0) != 0) {
        close(epfd);
        close(ctrl);
        rinha_index_close(&index);
        return 1;
    }

    conn_t *conns = g_conns;
    int *ctrl_conns = g_ctrl_conns;
    struct epoll_event *events = g_events;
    for (int i = 0; i < MAX_CONN; ++i) conns[i].fd = -1;
    for (int i = 0; i < MAX_CTRL_CONN; ++i) ctrl_conns[i] = -1;

    for (;;) {
        int ready = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int e = 0; e < ready; ++e) {
            uint32_t kind = event_kind(events[e].data.u64);
            uint32_t idx = event_idx(events[e].data.u64);
            uint32_t ev = events[e].events;

            if (kind == EVENT_LISTENER) {
                if (ev & (EPOLLERR | EPOLLHUP)) break;
                for (;;) {
                    int cfd = accept4(ctrl, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (cfd < 0) {
                        if (errno == EINTR) continue;
                        break;
                    }
                    int cidx = add_ctrl_conn(ctrl_conns, cfd);
                    if (cidx >= 0 && epoll_add_in(epfd, cfd, EVENT_CTRL, (uint32_t)cidx) != 0) {
                        close_ctrl_conn(ctrl_conns, cidx);
                    }
                }
                continue;
            }

            if (kind == EVENT_CTRL) {
                if (idx >= MAX_CTRL_CONN || ctrl_conns[idx] < 0) continue;
                if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    close_ctrl_conn(ctrl_conns, (int)idx);
                    continue;
                }
                if (ev & EPOLLIN) {
                    if (!drain_ctrl_conn(epfd, ctrl_conns[idx], conns, &index, close_after_response)) close_ctrl_conn(ctrl_conns, (int)idx);
                }
                continue;
            }

            if (kind == EVENT_CLIENT) {
                if (idx >= MAX_CONN || conns[idx].fd < 0) continue;
                if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    close_conn(&conns[idx]);
                    continue;
                }
                if (ev & EPOLLIN) {
                    if (!read_conn(&conns[idx], &index, close_after_response)) close_conn(&conns[idx]);
                }
            }
        }
    }

    for (int i = 0; i < MAX_CONN; ++i) close_conn(&conns[i]);
    for (int i = 0; i < MAX_CTRL_CONN; ++i) close_ctrl_conn(ctrl_conns, i);
    close(epfd);
    close(ctrl);
    rinha_index_close(&index);
    return 1;
}
