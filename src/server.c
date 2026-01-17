#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common.h"

const size_t k_max_msg = 4096;

static int32_t one_request(int conn_fd);
// static void do_something(int conn_fd);

int main() {
    /* 1. Obtain a socket handle */
    // AF_INET for IPv4, AF_INET6 for IPv6
    // SOCK_STREAM for TCP, SOCK_DGRAM for UDP
    // run "man ip.7" to view ip sockets manual
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket creation failed");
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
        die("bind failed");
    }

    /* 4. Listen */
    if (listen(fd, SOMAXCONN) < 0) {
        die("listen failed");
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

        // do_something(conn_fd);
        while (1) {
            int32_t err = one_request(conn_fd); // read 1 request and write 1 response
            if (err) {
                break;
            }
        }

        close(conn_fd);
    }

    return 0;
}

/* Implementation of a Length-Prefixed Framing Protocol */
static int32_t one_request(int conn_fd) {
    /* 1. Create the read buffer */
    char rbuf[4 + k_max_msg];  // 4 bytes header

    /* 2. Read exactly 4 bytes (the header) */
    // errno is a Thread Local Gloabl Variable
    // If the kernel fails, it returns -1 to the C program and
    // writes the specific error code into the errno variable.
    errno = 0;
    int32_t err = read_full(conn_fd, rbuf, 4);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    /* 3. Interpret the header as the length of the message */
    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    /* 4. Ready exactly len bytes (the body) */
    err = read_full(conn_fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    /* 5. Print the message */
    // print a string of a specific length
    printf("client says: %.*s\n", len, &rbuf[4]);

    /* 6. Prepare response */
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];

    /* 7. Write leangth header and body to the write buffer */
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);

    /* 8. Send the repsonse all at once */
    return write_all(conn_fd, wbuf, 4 + len);
}

/*
static void do_something(int conn_fd) {
    char rbuf[64] = {};
    ssize_t n = read(conn_fd, rbuf, sizeof(rbuf) - 1);  // leave the 64th byte as a guaranteed zero
    if (n < 0) {
        msg("Read failed");
        return;
    }
    fprintf(stderr, "Client says: %s\n", rbuf);

    char wbuf[] = "world";
    write(conn_fd, wbuf, strlen(wbuf));
}
*/
