/* SPDX-License-Identifier: MIT */

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <liburing.h>

#define LISTEN_PORT 3000
#define LISTEN_BACKLOG 511
#define THREAD_POOL_SIZE 24
#define WORKER_CONNECTIONS 1024
#define BUF_SIZE 1024
#define RESPONSE_BODY "Hello, world!\n"
#define SERVER "origin-liburing"
#define HTTP_DATE_BUF_LEN sizeof("Sun, 06 Nov 1994 08:49:37 GMT")

typedef int ngx_int_t;
typedef unsigned int ngx_uint_t;
typedef unsigned char u_char;

enum {
  ACCEPT,
  READ,
  WRITE,
};

typedef struct connection connection;

typedef struct connection {
  connection *next;
  int32_t fd;
  uint16_t type;
  uint16_t closing;
  u_char buf[BUF_SIZE];
} connection;

static void init_connections(connection *connections, int connection_n) {
  int i;
  connection *c, *next;

  i = connection_n;
  c = connections;
  next = NULL;

  do {
    i--;

    c[i].next = next;
    c[i].fd = -1;
    c[i].type = 0;

    next = &c[i];
  } while (i);
}

static connection *get_connection(connection **free_connections,
                                  int *free_connection_n) {
  connection *c;

  c = *free_connections;
  if (c == NULL) {
    fprintf(stderr, "worker_connections are not enough\n");
    return NULL;
  }
  *free_connections = c->next;
  (*free_connection_n)--;
  return c;
}

static void free_connection(connection *c, connection **free_connections,
                            int *free_connection_n) {
  c->next = *free_connections;
  *free_connections = c;
  (*free_connection_n)++;
}

static void close_connection(connection *c, connection **free_connections,
                             int *free_connection_n) {
  free_connection(c, free_connections, free_connection_n);
  close(c->fd);
}

static int listen_socket(struct sockaddr_in *addr, int port) {
  int fd, ret;

  fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  int32_t val = 1;
  ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  assert(ret != -1);

  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = inet_addr("127.0.0.1");
  addr->sin_port = htons(port);
  ret = bind(fd, (struct sockaddr *)addr, sizeof(*addr));
  assert(!ret);
  ret = listen(fd, LISTEN_BACKLOG);
  assert(ret != -1);

  return fd;
}

static void prep_accept(struct io_uring *ring, int fd,
                        struct sockaddr *client_addr,
                        socklen_t *client_addr_len, connection *c) {
  struct io_uring_sqe *sqe;

  sqe = io_uring_get_sqe(ring);
  io_uring_prep_accept(sqe, fd, client_addr, client_addr_len, 0);

  c->fd = fd;
  c->type = ACCEPT;
  c->closing = 0;
  sqe->user_data = (uint64_t)c;
}

static void prep_recv(struct io_uring *ring, int fd, connection *c) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_recv(sqe, fd, c->buf, BUF_SIZE, 0);

  c->fd = fd;
  c->type = READ;
  sqe->user_data = (uint64_t)c;
}

static void prep_send(struct io_uring *ring, int fd, connection *c,
                      size_t len) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (sqe == NULL) {
    fprintf(stderr, "cannot get sqe in prep_send\n");
    exit(1);
  }
  io_uring_prep_send(sqe, fd, c->buf, len, 0);

  c->fd = fd;
  c->type = WRITE;
  sqe->user_data = (uint64_t)c;
}

ngx_int_t ngx_strncasecmp(u_char *s1, u_char *s2, size_t n) {
  ngx_uint_t c1, c2;

  while (n) {
    c1 = (ngx_uint_t)*s1++;
    c2 = (ngx_uint_t)*s2++;

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

u_char *ngx_strlcasestrn(u_char *s1, u_char *last, u_char *s2, size_t n) {
  ngx_uint_t c1, c2;

  c2 = (ngx_uint_t)*s2++;
  c2 = (c2 >= 'A' && c2 <= 'Z') ? (c2 | 0x20) : c2;
  last -= n;

  do {
    do {
      if (s1 >= last) {
        return NULL;
      }

      c1 = (ngx_uint_t)*s1++;

      c1 = (c1 >= 'A' && c1 <= 'Z') ? (c1 | 0x20) : c1;

    } while (c1 != c2);

  } while (ngx_strncasecmp(s1, s2, n) != 0);

  return --s1;
}

#define CONNECTION_CLOSE "\r\nConnection: close\r\n"

static int has_connection_close(u_char *req, int n) {
  return ngx_strlcasestrn(req, req + n, (u_char *)CONNECTION_CLOSE,
                          sizeof(CONNECTION_CLOSE) - 2) != NULL;
}

static time_t get_now() {
  struct timeval tv;

  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}

static int format_http_date(time_t now, char buffer[HTTP_DATE_BUF_LEN]) {
  struct tm *tm;

  tm = gmtime(&now);
  if (tm == NULL) {
    perror("gmtime failed");
    return -1;
  }
  return (int)strftime(buffer, HTTP_DATE_BUF_LEN, "%a, %d %b %Y %H:%M:%S GMT",
                       tm);
}

static int serve(int server_sock) {
  int ret = 0;
  struct io_uring ring;
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  char http_date_buf[HTTP_DATE_BUF_LEN];
  int http_date_len;
  time_t now, prev_now = 0;

  ret = io_uring_queue_init(2048, &ring, 0);
  if (ret < 0) {
    fprintf(stderr, "init ring error: %s\n", strerror(-ret));
    return ret;
  }

  connection *connections = malloc(sizeof(connection) * WORKER_CONNECTIONS);
  if (connections == NULL) {
    fprintf(stderr, "cannot alloc connections\n");
    return -1;
  }
  int connection_n = WORKER_CONNECTIONS;

  init_connections(connections, connection_n);
  connection *free_connections = &connections[0];
  int free_connection_n = WORKER_CONNECTIONS;

  connection *c = get_connection(&free_connections, &free_connection_n);
  prep_accept(&ring, server_sock, (struct sockaddr *)&client_addr,
              &client_addr_len, c);
  while (1) {
    io_uring_submit_and_wait(&ring, 1);

    struct io_uring_cqe *cqe;
    unsigned head;
    unsigned count = 0;
    io_uring_for_each_cqe(&ring, head, cqe) {
      ++count;
      connection *c = (connection *)cqe->user_data;
      switch (c->type) {
      case ACCEPT: {
        int client_sock = cqe->res;
        if (client_sock < 0) {
          fprintf(stderr, "accept error: %s\n", strerror(-cqe->res));
        } else {
          prep_recv(&ring, client_sock, c);
        }
        connection *c2 = get_connection(&free_connections, &free_connection_n);
        prep_accept(&ring, server_sock, (struct sockaddr *)&client_addr,
                    &client_addr_len, c2);
        break;
      }
      case READ: {
        int bytes_read = cqe->res;
        if (bytes_read <= 0) {
          if (bytes_read < 0) {
            fprintf(stderr, "recv error: %s\n", strerror(-cqe->res));
          }
          close_connection(c, &free_connections, &free_connection_n);
        } else {
          c->closing = has_connection_close(c->buf, bytes_read);
          now = get_now();
          if (now != prev_now) {
            http_date_len = format_http_date(now, http_date_buf);
            if (http_date_len == -1) {
              continue;
            }
            prev_now = now;
          }
          int resp_len = snprintf((char *)c->buf, sizeof(c->buf),
                                  "HTTP/1.1 200 OK\r\n"
                                  "Date: %s\r\n"
                                  "Server: %s\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Content-Length: %ld\r\n"
                                  "\r\n"
                                  "%s",
                                  http_date_buf, SERVER,
                                  sizeof(RESPONSE_BODY) - 1, RESPONSE_BODY);
          prep_send(&ring, c->fd, c, resp_len);
        }
        break;
      }
      case WRITE:
        if (cqe->res < 0) {
          fprintf(stderr, "send error: %s\n", strerror(-cqe->res));
        }
        if (c->closing) {
          close_connection(c, &free_connections, &free_connection_n);
        } else {
          prep_recv(&ring, c->fd, c);
        }
        break;
      }
    }
    io_uring_cq_advance(&ring, count);
  }

  return ret;
}

static void *thread_func(void *arg) {
  int server_sock = *(int *)arg;
  serve(server_sock);
  return NULL;
}

int main(int argc, char *argv[]) {
  int ret;
  struct sockaddr_in addr;
  pthread_t threads[THREAD_POOL_SIZE];

  int32_t server_sock = listen_socket(&addr, LISTEN_PORT);

  for (int i = 0; i < THREAD_POOL_SIZE; i++) {
    ret = pthread_create(&threads[i], NULL, thread_func, (void *)&server_sock);
    if (ret != 0) {
      perror("Create thread failed");
      exit(EXIT_FAILURE);
    }
  }

  for (int i = 0; i < THREAD_POOL_SIZE; i++) {
    ret = pthread_join(threads[i], NULL);
    if (ret != 0) {
      perror("Join thread failed");
      exit(EXIT_FAILURE);
    }
  }

  ret = serve(server_sock);
  assert(ret == 0);
  close(server_sock);
  return ret;
}
