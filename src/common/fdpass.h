#ifndef RINHA_FDPASS_H
#define RINHA_FDPASS_H

int rinha_send_fd(int socket_fd, int passed_fd);
/* Receive one descriptor sent with SCM_RIGHTS.
   Returns >=0 for the received fd, -2 when the nonblocking socket has no fd yet,
   and -1 for EOF or fatal control-message errors. */
int rinha_recv_fd(int socket_fd);
int rinha_recv_fd_wait(int socket_fd, int timeout_ms);

#endif
