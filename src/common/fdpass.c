#include "common/fdpass.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef MSG_CMSG_CLOEXEC
#define MSG_CMSG_CLOEXEC 0
#endif

int rinha_send_fd(int socket_fd, int passed_fd) {
    char byte = 1;
    struct iovec iov;
    iov.iov_base = &byte;
    iov.iov_len = 1;

    union {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } control;
    memset(&control, 0, sizeof(control));

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.buf;
    msg.msg_controllen = sizeof(control.buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &passed_fd, sizeof(passed_fd));

    for (;;) {
        ssize_t n = sendmsg(socket_fd, &msg, MSG_NOSIGNAL);
        if (n == 1) return 0;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd;
            pfd.fd = socket_fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            int r;
            do {
                r = poll(&pfd, 1, 10);
            } while (r < 0 && errno == EINTR);
            if (r > 0 && (pfd.revents & POLLOUT)) continue;
        }
        return -1;
    }
}

int rinha_recv_fd(int socket_fd) {
    char byte = 0;
    struct iovec iov;
    iov.iov_base = &byte;
    iov.iov_len = 1;

    union {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } control;
    memset(&control, 0, sizeof(control));

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.buf;
    msg.msg_controllen = sizeof(control.buf);

    for (;;) {
        ssize_t n = recvmsg(socket_fd, &msg, MSG_CMSG_CLOEXEC);
        if (n > 0) break;
        if (n == 0) return -1;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
        return -1;
    }

    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS && cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
            int fd = -1;
            memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
            return fd;
        }
    }
    return -2;
}

int rinha_recv_fd_wait(int socket_fd, int timeout_ms) {
    if (timeout_ms < 0) timeout_ms = 0;
    int remaining = timeout_ms;
    for (;;) {
        int fd = rinha_recv_fd(socket_fd);
        if (fd != -2) return fd;
        if (remaining <= 0) return -2;

        struct pollfd pfd;
        pfd.fd = socket_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int wait_ms = remaining > 10 ? 10 : remaining;
        int r;
        do {
            r = poll(&pfd, 1, wait_ms);
        } while (r < 0 && errno == EINTR);
        if (r < 0) return -1;
        if (r == 0) {
            remaining -= wait_ms;
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fd = rinha_recv_fd(socket_fd);
            return fd >= 0 ? fd : -1;
        }
    }
}
