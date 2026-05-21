#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "common/fdpass.h"
#include "common/net.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) < 0) {}
}

static void test_listen_unix_accepts_seqpacket_clients(void) {
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    snprintf(path, sizeof(path), "/tmp/rinha-fdpass-%ld.sock", (long)getpid());

    int listener = rinha_listen_unix(path, 4);
    assert(listener >= 0);

    int client = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    assert(client >= 0);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    assert(connect(client, (struct sockaddr *)&addr, sizeof(addr)) == 0);

    int accepted = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
    assert(accepted >= 0);
    close(accepted);
    close(client);
    close(listener);
    unlink(path);
}

static void test_recv_fd_sets_cloexec_on_received_descriptor(void) {
    int sv[2];
    int pipefd[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
    assert(pipe(pipefd) == 0);
    assert(rinha_send_fd(sv[1], pipefd[0]) == 0);

    int got = rinha_recv_fd(sv[0]);
    assert(got >= 0);
    int flags = fcntl(got, F_GETFD, 0);
    assert(flags >= 0);
    assert((flags & FD_CLOEXEC) != 0);

    close(got);
    close(pipefd[0]);
    close(pipefd[1]);
    close(sv[0]);
    close(sv[1]);
}

static void test_recv_fd_wait_timeout(void) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
    assert(rinha_set_nonblocking_cloexec(sv[0]) == 0);
    assert(rinha_recv_fd_wait(sv[0], 1) == -2);
    close(sv[0]);
    close(sv[1]);
}

static void test_recv_fd_wait_delayed_sender(void) {
    int sv[2];
    int pipefd[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
    assert(pipe(pipefd) == 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(sv[0]);
        close(pipefd[1]);
        sleep_ms(5);
        int rc = rinha_send_fd(sv[1], pipefd[0]);
        close(pipefd[0]);
        close(sv[1]);
        _exit(rc == 0 ? 0 : 1);
    }

    close(sv[1]);
    close(pipefd[0]);
    assert(rinha_set_nonblocking_cloexec(sv[0]) == 0);
    int got = rinha_recv_fd_wait(sv[0], 50);
    assert(got >= 0);
    assert(write(pipefd[1], "x", 1) == 1);
    char ch = 0;
    assert(read(got, &ch, 1) == 1);
    assert(ch == 'x');

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    close(got);
    close(pipefd[1]);
    close(sv[0]);
}

int main(void) {
    test_listen_unix_accepts_seqpacket_clients();
    test_recv_fd_sets_cloexec_on_received_descriptor();
    test_recv_fd_wait_timeout();
    test_recv_fd_wait_delayed_sender();
    return 0;
}
