/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <liburing.h>

#define LISTEN_PORT 3000
#define LISTEN_BACKLOG 511
#define MAX_FDS 32
#define BUF_SIZE 1024

enum {
    ACCEPT,
    READ,
    WRITE,
};

typedef struct conn_info {
    int32_t  fd;
    uint16_t type;
    uint16_t reserved;
} conn_info;

static int listen_socket(struct sockaddr_in *addr, int port)
{
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

static void prep_accept(struct io_uring *ring, int fd, struct sockaddr *client_addr, socklen_t *client_addr_len)
{
	struct io_uring_sqe *sqe;

    sqe = io_uring_get_sqe(ring);
    io_uring_prep_accept(sqe, fd, client_addr, client_addr_len, 0);

	conn_info conn_i = {
		.fd = fd,
		.type = ACCEPT,
	};
	memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
	assert(sizeof(conn_i) == 8);
}

static void prep_recv(struct io_uring *ring, int fd, void *buf, size_t len) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_recv(sqe, fd, buf, len, 0);

    conn_info conn_i = {
        .fd = fd,
        .type = READ,
    };
    memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

static void prep_send(struct io_uring *ring, int fd, const void *buf, size_t len) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_send(sqe, fd, buf, len, 0);

    conn_info conn_i = {
        .fd = fd,
        .type = WRITE,
    };
    memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

static int serve(struct io_uring *ring, int server_sock)
{
	int ret = 0;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	char buf[BUF_SIZE];

	prep_accept(ring, server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
	while (1) {
		io_uring_submit_and_wait(ring, 1);

        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count = 0;
        io_uring_for_each_cqe(ring, head, cqe) {
            ++count;
            struct conn_info conn_i;
            memcpy(&conn_i, &cqe->user_data, sizeof(conn_i));
			switch (conn_i.type) {
			case ACCEPT: {
				int client_sock = cqe->res;
				if (client_sock < 0) {
					fprintf(stderr, "accept error: %s\n", strerror(-cqe->res));
				} else {
					prep_recv(ring, client_sock, buf, BUF_SIZE);
				}
				prep_accept(ring, server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
				break;
			}
			case READ: {
				int bytes_read = cqe->res;
				if (bytes_read <= 0) {
					if (bytes_read < 0) {
						fprintf(stderr, "recv error: %s\n", strerror(-cqe->res));
					}
					close(conn_i.fd);
				} else {
					prep_send(ring, conn_i.fd, buf, bytes_read);
				}
				break;
			}
			case WRITE:
				if (cqe->res < 0) {
					fprintf(stderr, "send error: %s\n", strerror(-cqe->res));
				}
				prep_recv(ring, conn_i.fd, buf, BUF_SIZE);
				break;
			}
        }
        io_uring_cq_advance(ring, count);
	}

	return ret;
}

int main(int argc, char *argv[])
{
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
