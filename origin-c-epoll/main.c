#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <linux/net.h>

#define MAX_EVENTS 10
#define BUF_SIZE 1024
#define PORT 3000
#define THREAD_POOL_SIZE 24
#define RESPONSE_BODY "Hello, world!\n"

void *handle_client(void *arg) {
    int server_fd, client_fd, epoll_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    struct epoll_event ev, events[MAX_EVENTS];
    int nfds, n, i;

    server_fd = *(int *)arg;

    epoll_fd = epoll_create1(0);
    // printf("epoll_fd=%d\n", epoll_fd);
    if (epoll_fd == -1) {
        perror("epoll_create1 failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
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
                client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
                // printf("accept client_fd=%d\n", client_fd);
                if (client_fd == -1) {
                    perror("accept");
                    continue;
                }

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl: client_fd");
                    close(client_fd);
                    continue;
                }
            } else {
                char buf[BUF_SIZE];
                n = read(events[i].data.fd, buf, BUF_SIZE);
                // printf("read n=%d, fd=%d\n", n, events[i].data.fd);
                if (n <= 0) {
                    if (n < 0) perror("read error");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    close(events[i].data.fd);
                } else {
                    int resp_len = snprintf(buf, sizeof(buf),
                        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s",
                        sizeof(RESPONSE_BODY) - 1, RESPONSE_BODY);
                    write(events[i].data.fd, buf, resp_len);
                }
            }
        }
    }
}

int main() {
    int server_fd, epoll_fd, rc, i, reuseaddr, reuseport;
    struct sockaddr_in server_addr;
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

    reuseport = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT,
                    (const void *) &reuseport, sizeof(int)) == -1) {
        perror("setsockopt reuse port failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
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