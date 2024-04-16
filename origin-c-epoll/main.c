#define _GNU_SOURCE /* for accept4 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <linux/net.h>
#include <linux/tcp.h>
#include <stdalign.h>

#define MAX_EVENTS 512
#define BUF_SIZE 1024
#define PORT 3000
#define THREAD_POOL_SIZE 24
#define WORKER_CONNECTIONS 1024
#define RESPONSE_BODY "Hello, world!\n"

typedef int ngx_int_t;
typedef unsigned int ngx_uint_t;
typedef unsigned char u_char;
typedef int ngx_socket_t;

typedef struct {
    void               *data;
    ngx_socket_t        fd;
    unsigned            tcp_nodelay:2;   /* ngx_connection_tcp_nodelay_e */
} ngx_connection_t;

typedef enum {
    NGX_TCP_NODELAY_UNSET = 0,
    NGX_TCP_NODELAY_SET,
    NGX_TCP_NODELAY_DISABLED
} ngx_connection_tcp_nodelay_e;

ngx_int_t
ngx_strncasecmp(u_char *s1, u_char *s2, size_t n)
{
    ngx_uint_t  c1, c2;

    while (n) {
        c1 = (ngx_uint_t) *s1++;
        c2 = (ngx_uint_t) *s2++;

        c1 = (c1 >= 'A' && c1 <= 'Z') ? (c1 | 0x20) : c1;
        c2 = (c2 >= 'A' && c2 <= 'Z') ? (c2 | 0x20) : c2;

        if (c1 == c2) {

            if (c1) {
                n--;
                continue;
            }

            return 0;
        }

        return c1 - c2;
    }

    return 0;
}

/*
 * ngx_strlcasestrn() is intended to search for static substring
 * with known length in string until the argument last. The argument n
 * must be length of the second substring - 1.
 */

u_char *
ngx_strlcasestrn(u_char *s1, u_char *last, u_char *s2, size_t n)
{
    ngx_uint_t  c1, c2;

    c2 = (ngx_uint_t) *s2++;
    c2 = (c2 >= 'A' && c2 <= 'Z') ? (c2 | 0x20) : c2;
    last -= n;

    do {
        do {
            if (s1 >= last) {
                return NULL;
            }

            c1 = (ngx_uint_t) *s1++;

            c1 = (c1 >= 'A' && c1 <= 'Z') ? (c1 | 0x20) : c1;

        } while (c1 != c2);

    } while (ngx_strncasecmp(s1, s2, n) != 0);

    return --s1;
}

#define CONNECTION_CLOSE "\r\nConnection: close\r\n"

static int has_connection_close(char *req, int n) {
    return ngx_strlcasestrn(req, req + n, CONNECTION_CLOSE, sizeof(CONNECTION_CLOSE) - 2) != NULL;
}

static void init_connections(ngx_connection_t *connections, ngx_uint_t connection_n) {
    ngx_uint_t           i;
    ngx_connection_t    *c, *next;

    i = connection_n;
    c = connections;
    next = NULL;

    do {
        i--;

        c[i].data = next;
        c[i].fd = (ngx_socket_t) -1;
        c[i].tcp_nodelay = NGX_TCP_NODELAY_UNSET;

        next = &c[i];
    } while (i);
}

static ngx_connection_t *get_connection(ngx_connection_t **free_connections, ngx_uint_t *free_connection_n) {
    ngx_connection_t  *c;

    c = *free_connections;
    if (c == NULL) {
        fprintf(stderr, "worker_connections are not enought\n");
        return NULL;
    }
    *free_connections = c->data;
    *free_connection_n--;    
    c->tcp_nodelay = NGX_TCP_NODELAY_UNSET;
    return c;
}

static void free_connection(ngx_connection_t *c, ngx_connection_t **free_connections, ngx_uint_t *free_connection_n) {
    c->data = *free_connections;
    *free_connections = c;
    *free_connection_n++;
}

static void close_connection(ngx_connection_t *c, ngx_connection_t **free_connections, ngx_uint_t *free_connection_n) {
    free_connection(c, free_connections, free_connection_n);
    close(c->fd);
}

void *handle_client(void *arg) {
    ngx_uint_t server_fd_requests = 0;
    int server_fd, client_fd, epoll_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    struct epoll_event ev, events[MAX_EVENTS];
    int nfds, n, i, closing, tcp_nodelay;
    ngx_connection_t *c, *free_connections, connections[WORKER_CONNECTIONS];
    ngx_uint_t free_connection_n, connection_n;

    server_fd = *(int *)arg;

    connection_n = WORKER_CONNECTIONS;
    init_connections(connections, connection_n);
    free_connections = &connections[0];
    free_connection_n = connection_n;

    epoll_fd = epoll_create1(0);
    // printf("epoll_fd=%d\n", epoll_fd);
    if (epoll_fd == -1) {
        perror("epoll_create1 failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN | EPOLLEXCLUSIVE;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: add server_fd");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        // printf("epoll_wait nfds=%d\n", nfds);
        if (nfds == -1) {
            perror("epoll_wait");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        for (i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_fd) {
                client_fd = accept4(server_fd, (struct sockaddr *) &client_addr, &client_addr_len, SOCK_NONBLOCK);
                // printf("accept client_fd=%d\n", client_fd);
                if (client_fd == -1) {
                    if (errno != EAGAIN) {
                        perror("accept");
                    }
                    continue;
                }

                /*
                 * Re-add the socket periodically so that other worker threads
                 * will get a chance to accept connections.
                 * See ngx_reorder_accept_events.
                 */
                if (server_fd_requests++ % 16 == 0) {
                    ev.events = 0;
                    ev.data.ptr = NULL;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_fd, &ev) == -1) {
                        perror("epoll_ctl: del server_fd");
                        close(server_fd);
                        exit(EXIT_FAILURE);
                    }

                    ev.events = EPOLLIN | EPOLLEXCLUSIVE;
                    ev.data.fd = server_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
                        perror("epoll_ctl: add server_fd");
                        close(server_fd);
                        exit(EXIT_FAILURE);
                    }
                }

                c = get_connection(&free_connections, &free_connection_n);
                c->fd = client_fd;
                ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                ev.data.ptr = c;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl: client_fd");
                    close_connection(c, &free_connections, &free_connection_n);
                    continue;
                }
            } else {
                alignas(1024) char buf[BUF_SIZE];
                c = events[i].data.ptr;
                client_fd = c->fd;
                n = recvfrom(client_fd, buf, BUF_SIZE, 0, NULL, NULL);
                if (n <= 0) {
                    if (n < 0) perror("read error");
                    close_connection(c, &free_connections, &free_connection_n);
                } else {
                    closing = has_connection_close(buf, n);
                    int resp_len = snprintf(buf, sizeof(buf),
                        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s",
                        sizeof(RESPONSE_BODY) - 1, RESPONSE_BODY);
                    struct iovec iov;
                    iov.iov_base = buf;
                    iov.iov_len = resp_len;
                    if (writev(client_fd, &iov, 1) == -1) {
                        perror("writev");
                        close_connection(c, &free_connections, &free_connection_n);
                        continue;
                    }
                    if (closing) {
                        close_connection(c, &free_connections, &free_connection_n);
                    } else {
                        if (c->tcp_nodelay == NGX_TCP_NODELAY_UNSET) {
                            tcp_nodelay = 1;
                            if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                                        (const void *) &tcp_nodelay, sizeof(int))
                                == -1)
                            {
                                perror("setsockopt TCP_NODELAY: client_fd");
                                close_connection(c, &free_connections, &free_connection_n);
                                continue;
                            }
                        }
                        c->tcp_nodelay = NGX_TCP_NODELAY_SET;
                    }
                }
            }
        }
    }
}

int main() {
    int server_fd, epoll_fd, rc, i, reuseaddr;
    struct sockaddr_in server_addr;
    unsigned long nb;
    pthread_t threads[THREAD_POOL_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    // printf("server_fd=%d\n", server_fd);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    reuseaddr = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                    (const void *) &reuseaddr, sizeof(int)) == -1) {
        perror("setsockopt reuse addr failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    nb = 1;
    if (ioctl(server_fd, FIONBIO, &nb) == -1) {
        perror("ioctl FIONBIO failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 511) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        rc = pthread_create(&threads[i], NULL, handle_client, (void *)&server_fd);
        if (rc != 0) {
            perror("Create thread failed");
            exit(EXIT_FAILURE);            
        }
    }

    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            perror("Join thread failed");
            exit(EXIT_FAILURE);            
        }
    }

    close(server_fd);
    return 0;
}
