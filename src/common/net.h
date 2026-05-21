#ifndef RINHA_NET_H
#define RINHA_NET_H

int rinha_set_nonblocking_cloexec(int fd);
void rinha_tune_tcp_socket(int fd);
int rinha_listen_unix(const char *path, int backlog);

#endif
