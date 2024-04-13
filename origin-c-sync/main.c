#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 3000
#define BUFSIZE 1024
#define RESPONSE_BODY "Hello, world!\n"

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size;
    char buffer[BUFSIZE];
    int read_len;

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

    client_addr_size = sizeof(client_addr);
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_size);
        if (client_fd < 0) {
            perror("Client accept failed");
            exit(EXIT_FAILURE);
        }

        while ((read_len = read(client_fd, buffer, BUFSIZE)) != 0) {
            int resp_len = snprintf(buffer, sizeof(buffer),
                "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s",
                sizeof(RESPONSE_BODY) - 1, RESPONSE_BODY);
            write(client_fd, buffer, resp_len);
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
