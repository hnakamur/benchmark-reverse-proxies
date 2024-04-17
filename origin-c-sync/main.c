#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <linux/tcp.h>
#include <sys/time.h>
#include <time.h>

#define PORT 3000
#define BUFSIZE 1024
#define THREAD_POOL_SIZE 24
#define RESPONSE_BODY "Hello, world!\n"
#define SERVER "origin-c-sync"
#define HTTP_DATE_BUF_LEN sizeof("Sun, 06 Nov 1994 08:49:37 GMT")

typedef int ngx_int_t;
typedef unsigned int ngx_uint_t;
typedef unsigned char u_char;

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
    return (int)strftime(buffer, HTTP_DATE_BUF_LEN, "%a, %d %b %Y %H:%M:%S GMT", tm);
}

static int set_tcp_nodelay(int sockfd) {
    int tcp_nodelay = 1;
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
                      (const void *) &tcp_nodelay, sizeof(int));
}

void *handle_client(void *arg) {
    int server_fd, client_fd;
    char buffer[BUFSIZE];
    int read_len, closing, first_write;
    struct sockaddr_in client_addr;
    socklen_t client_addr_size;
    char http_date_buf[HTTP_DATE_BUF_LEN];
    int http_date_len;
    time_t now, prev_now = 0;

    server_fd = *(int *)arg;
    client_addr_size = sizeof(client_addr);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_size);
        if (client_fd < 0) {
            perror("Client accept failed");
            exit(EXIT_FAILURE);
        }

        first_write = 1;
        while (1) {
            read_len = read(client_fd, buffer, BUFSIZE);
            if (read_len <= 0) {
                if (read_len < 0) {
                    perror("read error");
                }
                close(client_fd);
                break;
            } else {
                closing = has_connection_close(buffer, read_len);
                now = get_now();
                if (now != prev_now) {
                    http_date_len = format_http_date(now, http_date_buf);
                    if (http_date_len == -1) {
                        continue;
                    }
                    prev_now = now;
                }
                int resp_len = snprintf(buffer, sizeof(buffer),
                    "HTTP/1.1 200 OK\r\n"
                    "Date: %s\r\n"
                    "Server: %s\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %d\r\n"
                    "\r\n"
                    "%s",
                    http_date_buf,
                    SERVER,
                    sizeof(RESPONSE_BODY) - 1, RESPONSE_BODY);
                if (write(client_fd, buffer, resp_len) == -1) {
                    perror("write");
                    close(client_fd);
                    break;
                }
                if (closing) {
                    close(client_fd);
                    break;
                } else if (first_write) {
                    if (set_tcp_nodelay(client_fd) == -1) {
                        perror("setsockopt TCP_NODELAY");
                        close(client_fd);
                        break;
                    }
                    first_write = 0;
                }
            }
        }
    }
    close(client_fd);

    return NULL;
}

int main() {
    int server_fd, rc;
    struct sockaddr_in server_addr;
    pthread_t threads[THREAD_POOL_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Socket bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 256) < 0) {
        perror("Listen failed");
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
