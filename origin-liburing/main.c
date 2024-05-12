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
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <liburing.h>

#define LISTEN_PORT 3000
#define LISTEN_BACKLOG 511
#define WORKER_CONNECTIONS 1024
#define BUF_SIZE 1024

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
  uint16_t padding;
  char buf[BUF_SIZE];
} connection;

static connection connections[WORKER_CONNECTIONS];
static int connection_n = WORKER_CONNECTIONS;

static connection *free_connections = &connections[0];
static int free_connection_n = WORKER_CONNECTIONS;

static void init_connections() {
  uint16_t i;
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

static connection *get_connection() {
  connection *c;

  c = free_connections;
  assert(c != NULL);
  free_connections = c->next;
  free_connection_n--;
  return c;
}

static void free_connection(connection *c) {
  c->next = free_connections;
  free_connections = c;
  free_connection_n++;
}

static void close_connection(connection *c) {
  free_connection(c);
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
                        socklen_t *client_addr_len) {
  struct io_uring_sqe *sqe;

  sqe = io_uring_get_sqe(ring);
  io_uring_prep_accept(sqe, fd, client_addr, client_addr_len, 0);

  connection *c = get_connection();
  c->fd = fd, c->type = ACCEPT;
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
  io_uring_prep_send(sqe, fd, c->buf, len, 0);

  c->fd = fd;
  c->type = WRITE;
  sqe->user_data = (uint64_t)c;
}

static int serve(struct io_uring *ring, int server_sock) {
  int ret = 0;
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  init_connections(connections, connection_n);

  prep_accept(ring, server_sock, (struct sockaddr *)&client_addr,
              &client_addr_len);
  while (1) {
    io_uring_submit_and_wait(ring, 1);

    struct io_uring_cqe *cqe;
    unsigned head;
    unsigned count = 0;
    io_uring_for_each_cqe(ring, head, cqe) {
      ++count;
      connection *c = (connection *)cqe->user_data;
      switch (c->type) {
      case ACCEPT: {
        int client_sock = cqe->res;
        if (client_sock < 0) {
          fprintf(stderr, "accept error: %s\n", strerror(-cqe->res));
        } else {
          prep_recv(ring, client_sock, c);
        }
        prep_accept(ring, server_sock, (struct sockaddr *)&client_addr,
                    &client_addr_len);
        break;
      }
      case READ: {
        int bytes_read = cqe->res;
        if (bytes_read <= 0) {
          if (bytes_read < 0) {
            fprintf(stderr, "recv error: %s\n", strerror(-cqe->res));
          }
          close_connection(c);
        } else {
          prep_send(ring, c->fd, c, bytes_read);
        }
        break;
      }
      case WRITE:
        if (cqe->res < 0) {
          fprintf(stderr, "send error: %s\n", strerror(-cqe->res));
        }
        prep_recv(ring, c->fd, c);
        break;
      }
    }
    io_uring_cq_advance(ring, count);
  }

  return ret;
}

int main(int argc, char *argv[]) {
  int ret;
  struct io_uring ring;
  struct sockaddr_in addr;

  ret = io_uring_queue_init(32, &ring, 0);
  assert(ret >= 0);

  int32_t server_sock = listen_socket(&addr, LISTEN_PORT);
  ret = serve(&ring, server_sock);
  assert(ret == 0);
  close(server_sock);

  io_uring_queue_exit(&ring);
  return ret;
}
