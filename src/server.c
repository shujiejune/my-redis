#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

static void do_something(int conn_fd);

int main() {
    /* 1. Obtain a socket handle */
    // AF_INET for IPv4, AF_INET6 for IPv6
    // SOCK_STREAM for TCP, SOCK_DGRAM for UDP
    // run "man ip.7" to view ip sockets manual
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket creation failed");
        exit(1);
    }

    /* 2. Set socket options */
    // Allow immediate restart of the server
    int val = 1;
    // fd: the listening socket
    // SOL_SOCKET: Socket Level. It tells the OS this is a change on the socket, not the TCP/IP protocol.
    // SO_REUSEADDR: the specific setting to change (reuse address)
    // &val: a pointer to 1 (True). Turn the feature ON.
    // sizeof(val): the OS needs to know the size of the option.
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    /* 3. Bind to an address */
    struct sockaddr_in addr = {0}; // zero out this entire struct
    addr.sin_family = AF_INET;  // IPv4
    addr.sin_port = htons(6379);  // port number
    addr.sin_addr.s_addr = htonl(0);  // 0.0.0.0
    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv) {
        perror("bind failed");
        exit(1);
    }

    /* 4. Listen */
    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen failed");
        exit(1);
    }
    printf("Server listening on port 6379...\n");

    /* 5. Accept connections */
    while (1) {
        struct sockaddr_in client_addr = {0};
        socklen_t addrlen = sizeof(client_addr);
        int conn_fd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if (conn_fd < 0) {
            perror("accept failed");
            continue;
        }
        printf("Client connected! Handle: %d\n", conn_fd);

        do_something(conn_fd);

        close(conn_fd);
    }
}

static void do_something(int conn_fd) {
    char rbuf[64] = {};
    ssize_t n = read(conn_fd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        perror("Read failed");
        return;
    }
    printf("Client says: %s\n", rbuf);

    char wbuf[] = "world";
    write(conn_fd, wbuf, strlen(wbuf));
}
