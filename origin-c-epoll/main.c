#define _GNU_SOURCE /* for accept4 */
#include <errno.h>
#include <linux/net.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#define MAX_EVENTS 512
#define BUF_SIZE 1024
#define PORT 3000
#define WORKER_CONNECTIONS 1024
#define RESPONSE_BODY "Hello, world!\n"
#define SERVER "toyserver"
#define HTTP_DATE_BUF_LEN sizeof("Sun, 06 Nov 1994 08:49:37 GMT")

typedef int ngx_int_t;
typedef unsigned int ngx_uint_t;
typedef unsigned char u_char;
typedef int ngx_socket_t;

typedef struct {
  void *data;
  ngx_socket_t fd;
  unsigned tcp_nodelay : 2; /* ngx_connection_tcp_nodelay_e */
} ngx_connection_t;

typedef enum {
  NGX_TCP_NODELAY_UNSET = 0,
  NGX_TCP_NODELAY_SET,
  NGX_TCP_NODELAY_DISABLED
} ngx_connection_tcp_nodelay_e;

static char *skip_ows(char *s, int n) {
  char *end = s + n;
  while (s < end && (*s == ' ' || *s == '\t')) {
    s++;
  }
  return s;
}

#define CONNECTION "connection"
#define CONNECTION_LEN (sizeof(CONNECTION) - 1)
#define CLOSE "close"
#define CLOSE_LEN (sizeof(CLOSE) - 1)

static char *find_crlf(char *s, int n) {
  char *p = memchr(s, '\r', n);
  if (p != NULL && p + 1 < s + n && p[1] == '\n') {
    return p;
  }
  return NULL;
}

static int has_connection_close(char *req, int n) {
  // printf("has_connection_close start, req=[%.*s]\n", n, req);
  char *field_end = find_crlf(req, n);
  if (field_end == NULL) {
    return 0;
  }
  n -= (field_end - req) + 2;
  char *p = field_end + 2;
  while ((field_end = find_crlf(p, n)) != NULL) {
    // printf("=== req=[%.*s], field_end=[%.*s], end_pos=%ld\n", n, req, (int)(n - (field_end - req)), field_end, field_end - req);
    if (field_end == p) {
      break;
    }

    if (p + CONNECTION_LEN + 1 < field_end &&
        (p[0] | 0x20) == 'c' &&
        (p[1] | 0x20) == 'o' &&
        (p[2] | 0x20) == 'n' &&
        (p[3] | 0x20) == 'n' &&
        (p[4] | 0x20) == 'e' &&
        (p[5] | 0x20) == 'c' &&
        (p[6] | 0x20) == 't' &&
        (p[7] | 0x20) == 'i' &&
        (p[8] | 0x20) == 'o' &&
        (p[9] | 0x20) == 'n' &&
        p[10] == ':')
    {
      // printf("= colon+1=[%.*s]\n", (int)(field_end - colon - 1), colon + 1);
      p += CONNECTION_LEN + 1;
      p = skip_ows(p, field_end - p);
      // printf("val=[%.*s]\n", (int)(field_end - val), val);
      int val_len = field_end - p;
      if (p + CLOSE_LEN <= field_end &&
          (p[0] | 0x20) == 'c' &&
          (p[1] | 0x20) == 'l' &&
          (p[2] | 0x20) == 'o' &&
          (p[3] | 0x20) == 's' &&
          (p[4] | 0x20) == 'e' &&
          skip_ows(p + CLOSE_LEN, val_len - CLOSE_LEN) == field_end)
      {
        return 1;
      }
    }

    n -= (field_end - p) + 2;
    p = field_end + 2;
  }
  return 0;
}

static void init_connections(ngx_connection_t *connections,
                             ngx_uint_t connection_n) {
  ngx_uint_t i;
  ngx_connection_t *c, *next;

  i = connection_n;
  c = connections;
  next = NULL;

  do {
    i--;

    c[i].data = next;
    c[i].fd = (ngx_socket_t)-1;
    c[i].tcp_nodelay = NGX_TCP_NODELAY_UNSET;

    next = &c[i];
  } while (i);
}

static ngx_connection_t *get_connection(ngx_connection_t **free_connections,
                                        ngx_uint_t *free_connection_n) {
  ngx_connection_t *c;

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

static void free_connection(ngx_connection_t *c,
                            ngx_connection_t **free_connections,
                            ngx_uint_t *free_connection_n) {
  c->data = *free_connections;
  *free_connections = c;
  *free_connection_n++;
}

static void close_connection(ngx_connection_t *c,
                             ngx_connection_t **free_connections,
                             ngx_uint_t *free_connection_n) {
  free_connection(c, free_connections, free_connection_n);
  close(c->fd);
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

void *handle_client(void *arg) {
  ngx_uint_t server_fd_requests = 0;
  int server_fd, client_fd, epoll_fd;
  struct sockaddr_in server_addr, client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  struct epoll_event ev, events[MAX_EVENTS];
  int nfds, n, i, closing, tcp_nodelay;
  ngx_connection_t *c, *free_connections, connections[WORKER_CONNECTIONS];
  ngx_uint_t free_connection_n, connection_n;
  char http_date_buf[HTTP_DATE_BUF_LEN];
  int http_date_len;
  time_t now, prev_now = 0;
  alignas(1024) char buf[BUF_SIZE];
  struct iovec iov;
  iov.iov_base = buf;

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
        client_fd = accept4(server_fd, (struct sockaddr *)&client_addr,
                            &client_addr_len, SOCK_NONBLOCK);
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
        c = events[i].data.ptr;
        client_fd = c->fd;
        n = recvfrom(client_fd, buf, BUF_SIZE, 0, NULL, NULL);
        if (n <= 0) {
          if (n < 0)
            perror("read error");
          close_connection(c, &free_connections, &free_connection_n);
        } else {
          closing = has_connection_close(buf, n);
          // printf("closing=%d\n", closing);
          now = get_now();
          if (now != prev_now) {
            http_date_len = format_http_date(now, http_date_buf);
            if (http_date_len == -1) {
              continue;
            }
            prev_now = now;
          }
          int resp_len = snprintf(buf, sizeof(buf),
                                  "HTTP/1.1 200 OK\r\n"
                                  "Date: %.*s\r\n"
                                  "Server: %.*s\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Content-Length: %ld\r\n"
                                  "\r\n"
                                  "%s",
                                  http_date_len, http_date_buf,
                                  (int)(sizeof(SERVER) - 1), SERVER,
                                  sizeof(RESPONSE_BODY) - 1, RESPONSE_BODY);
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
                             (const void *)&tcp_nodelay, sizeof(int)) == -1) {
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

static long get_logical_cpu_cores() { return sysconf(_SC_NPROCESSORS_ONLN); }

static long get_num_cpus_from_env() {
  char *val = getenv("NUM_CPUS");
  if (val == NULL) {
    return -1;
  }
  return atoi(val);
}

int main() {
  int server_fd, epoll_fd, rc, i, reuseaddr;
  struct sockaddr_in server_addr;
  unsigned long nb;
  int thread_count = get_num_cpus_from_env();
  if (thread_count == -1) {
    thread_count = get_logical_cpu_cores();
  }
  printf("thread_count=%d\n", thread_count);
  pthread_t *threads = malloc(sizeof(pthread_t) * thread_count);
  if (threads == NULL) {
    fprintf(stderr, "cannot allocate threads\n");
    exit(EXIT_FAILURE);
  }

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
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuseaddr,
                 sizeof(int)) == -1) {
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

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 511) < 0) {
    perror("listen failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  for (int i = 0; i < thread_count; i++) {
    rc = pthread_create(&threads[i], NULL, handle_client, (void *)&server_fd);
    if (rc != 0) {
      perror("Create thread failed");
      exit(EXIT_FAILURE);
    }
  }

  for (int i = 0; i < thread_count; i++) {
    rc = pthread_join(threads[i], NULL);
    if (rc != 0) {
      perror("Join thread failed");
      exit(EXIT_FAILURE);
    }
  }

  close(server_fd);
  return 0;
}
