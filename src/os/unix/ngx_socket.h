
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SOCKET_H_INCLUDED_
#define _NGX_SOCKET_H_INCLUDED_


#include <ngx_config.h>


#define NGX_WRITE_SHUTDOWN SHUT_WR

typedef int  ngx_socket_t;

#if (NGX_HAVE_DEMIKERNEL)

/* todo: move */
ssize_t ngx_demikernel_recv(ngx_connection_t *c, u_char *buf, size_t size);
ssize_t ngx_demikernel_recv_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit);
ssize_t ngx_demikernel_send(ngx_connection_t *c, u_char *buf, size_t size);
ngx_chain_t *ngx_demikernel_send_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit);
int ngx_demikernel_socket(int domain, int type, int protocol);
int ngx_demikernel_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int ngx_demikernel_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

#define ngx_socket          ngx_demikernel_socket
#define ngx_socket_n        "ngx_demikernel_socket()"

#else

#define ngx_socket          socket
#define ngx_socket_n        "socket()"

#endif


#if (NGX_HAVE_FIONBIO)

int ngx_nonblocking(ngx_socket_t s);
int ngx_blocking(ngx_socket_t s);

#define ngx_nonblocking_n   "ioctl(FIONBIO)"
#define ngx_blocking_n      "ioctl(!FIONBIO)"

#else

#define ngx_nonblocking(s)  fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK)
#define ngx_nonblocking_n   "fcntl(O_NONBLOCK)"

#define ngx_blocking(s)     fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK)
#define ngx_blocking_n      "fcntl(!O_NONBLOCK)"

#endif

#if (NGX_HAVE_FIONREAD)

#define ngx_socket_nread(s, n)  ioctl(s, FIONREAD, n)
#define ngx_socket_nread_n      "ioctl(FIONREAD)"

#endif

int ngx_tcp_nopush(ngx_socket_t s);
int ngx_tcp_push(ngx_socket_t s);

#if (NGX_LINUX)

#define ngx_tcp_nopush_n   "setsockopt(TCP_CORK)"
#define ngx_tcp_push_n     "setsockopt(!TCP_CORK)"

#else

#define ngx_tcp_nopush_n   "setsockopt(TCP_NOPUSH)"
#define ngx_tcp_push_n     "setsockopt(!TCP_NOPUSH)"

#endif


#define ngx_shutdown_socket    shutdown
#define ngx_shutdown_socket_n  "shutdown()"

#if (NGX_HAVE_DEMIKERNEL)

#define ngx_close_socket    demi_close
#define ngx_close_socket_n  "demi_close() socket"
#define setsockopt          ngx_demikernel_setsockopt
#define getsockopt          ngx_demikernel_getsockopt

#else

#define ngx_close_socket    close
#define ngx_close_socket_n  "close() socket"

#endif

#endif /* _NGX_SOCKET_H_INCLUDED_ */
