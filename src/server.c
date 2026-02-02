#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common.h"

#define k_max_msg 4096

enum {
    STATE_REQ = 0,  // reading request
    STATE_RES = 1,  // sending response
    STATE_END = 2   // mark for deletion
};

typedef enum {
    REQ_PROCESSED = 1,  // ate data
    REQ_INCOMPLETE = 0, // not enough data, stop loop
    REQ_ERROR = -1      // malformed message, kill connection
} ReqStatus;

// Context of a connection
typedef struct Conn {
    int fd;
    int state;  // STATE_REQ or STATE_RES
    // rbuf and wbuf are in the userspace (heap memory), not in the kernel
    // read buffer
    size_t rbuf_size;  // how many bytes currently in rbuf
    uint8_t rbuf[4 + k_max_msg];
    // write buffer
    size_t wbuf_size;  // total bytes to send
    size_t wbuf_sent;  // offset (already sent)
    uint8_t wbuf[4 + k_max_msg];
} Conn;

// Use fd as the index (key)
// Use a dynamic array of pointer (Conn *) as a map <fd, Conn *>
// fds are managed by OS kernel, live in a kernel-side table
// integer variables are managed by compiler/CPU, live in stack/heap memory
// they are not interwined
Conn **fd2conn = NULL;
size_t fd2conn_size = 0;

// Set a connection to NULL
// Not closing it, just remove from the map
static void conn_put(Conn *conn) {
    // Why comparing map size with file descriptor: fd is the index
    if (fd2conn_size <= (size_t)conn->fd) {
        // Resize fd2conn array if necessary
        size_t new_size = conn->fd + 1;
        fd2conn = realloc(fd2conn, new_size * sizeof(Conn *));

        // Initialize new space to NULL
        for (size_t i = fd2conn_size; i < new_size; i++) {
            fd2conn[i] = NULL;
        }
        fd2conn_size = new_size;
    }
    fd2conn[conn->fd] = conn;
}

static void conn_destroy(Conn *conn) {
    if (conn->fd >= 0) {
        close(conn->fd);
        if ((size_t)conn->fd < fd2conn_size) {
            fd2conn[conn->fd] = NULL;
        }
    }
    free(conn);
}

static int32_t accept_new_conn(int fd) {
    struct sockaddr_in client_addr = {0};
    socklen_t addrlen = sizeof(client_addr);
    int conn_fd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    if (conn_fd < 0) {
        msg("accept error");
        return -1;
    }

    // Set non-blocking mode
    fd_set_nb(conn_fd);

    Conn *conn = malloc(sizeof(Conn));
    if (!conn) {
        close(conn_fd);
        return -1;
    }
    conn->fd = conn_fd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;

    conn_put(conn);
    return 0;
}

static ReqStatus try_one_request(Conn *conn) {
    // 1. Check for the 4-byte header
    if (conn->rbuf_size < 4) {
        return REQ_INCOMPLETE;
    }

    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg) {
        msg("too long");
        return REQ_ERROR;
    }

    // 2. Check for rest of the message
    if (4 + len > conn->rbuf_size) {
        return REQ_INCOMPLETE;  // wait for more data
    }

    // 3. Got a full message
    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    // 4. Generate response
    const char *reply = "world";
    uint32_t reply_len = (uint32_t)strlen(reply);

    memcpy(&conn->wbuf[0], &reply_len, 4);
    memcpy(&conn->wbuf[4], reply, reply_len);
    conn->wbuf_size = 4 + reply_len;
    conn->wbuf_sent = 0;

    // 5. Remove the request from rbuf (left shift)
    size_t remain = conn->rbuf_size - (4 + len);
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    // 6. Change connection state to response
    conn->state = STATE_RES;

    return REQ_PROCESSED;
}

static void handle_read(Conn *conn) {
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], sizeof(conn->rbuf) - conn->rbuf_size);
    if (rv <= 0) {
        if (rv == 0) {  // Handle EOF
            msg("client closed connection");
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {  // Not ready, try again later.
            return;
        } else {
            msg("read error");
        }
        conn->state = STATE_END;
        return;
    }

    conn->rbuf_size += (size_t)rv;

    // Pipelining loop
    // While there is enough data for a full request, keep processing.
    while (1) {
        ReqStatus status = try_one_request(conn);

        if (status == REQ_INCOMPLETE) break;  // Normal break
        if (status == REQ_ERROR) {
            conn->state = STATE_END;  // Mark for closing
            break;
        }
        // If REQ_PROCESSED, continue looping to see if there is another request

        // Check: If a response is generated and the connection is switched to "Response Mode"
        // stop reading and go send the response.
        if (conn->state == STATE_RES) {
            break;  // Stop processing new requests so the wbuf is not overwritten
        }
    }
}

static void handle_write(Conn *conn) {
    assert(conn->wbuf_size > conn->wbuf_sent);
    ssize_t rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], conn->wbuf_size - conn->wbuf_sent);
    if (rv <= 0) {
        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {  // Not ready, try again later.
            return;
        }
        conn->state = STATE_END;
        return;
    }

    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);

    // If finished sending the whole response, switch back to the reading mode
    if (conn->wbuf_sent == conn->wbuf_size) {
        conn->state = STATE_REQ;
        conn->wbuf_size = 0;
        conn->wbuf_sent = 0;
    }
}

// static int32_t one_request(int conn_fd);
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
    // Make the main listener non-blocking
    fd_set_nb(fd);
    printf("Server listening on port 6379...\n");

    // Event Loop
    struct pollfd poll_args[64];  // can handle up to 64 connections

    /* 5. Accept connections */
    // Each iteration is a cycle of
    // Prepare: build the list of connections to watch
    // Wait: sleep until something happens
    // Dispatch: handle the events
    while (1) {
        // Reset poll arguments
        size_t n_poll = 0;

        // Add the listening socket
        poll_args[n_poll].fd = fd;
        poll_args[n_poll].events = POLLIN;  // wake up if a client connects
        poll_args[n_poll].revents = 0;  // clear previous results
        n_poll++;

        // Add all active client connections
        for (size_t i = 0; i < fd2conn_size; i++) {
            Conn *conn = fd2conn[i];
            if (!conn) continue;

            if (n_poll >= 64) break;

            poll_args[n_poll].fd = conn->fd;
            poll_args[n_poll].revents = 0;
            poll_args[n_poll].events = 0;

            // events is the translation of state to OS
            // If state is STATE_REQ | STATE_RES, please watch for POLLIN | POLLOUT
            if (conn->state == STATE_REQ) {
                poll_args[n_poll].events |= POLLIN;
            } else if (conn->state == STATE_RES) {
                poll_args[n_poll].events |= POLLOUT;
            }
            n_poll++;
        }

        // Wait (the only blocking call)
        int rv = poll(poll_args, (nfds_t)n_poll, -1);
        if (rv < 0 && errno == EINTR) continue;
        if (rv < 0) die("poll");

        // Handle listening socket
        if (poll_args[0].revents & POLLIN) {
            accept_new_conn(fd);
        }

        // Handle client sockets
        for (size_t i = 1; i < n_poll; i++) {
            if (poll_args[i].revents == 0) continue;

            // Find the connection using fd as the index
            int conn_fd = poll_args[i].fd;
            Conn *conn = fd2conn[conn_fd];

            // revents is the answer from OS
            if (poll_args[i].revents & POLLIN) {  // data has arrived
                handle_read(conn);
            }
            if (poll_args[i].revents & POLLOUT) { // buffer space is available
                handle_write(conn);
            }

            // Cleanup if marked for death
            if (conn->state == STATE_END) {
                conn_destroy(conn);
            }
        }
    }

    return 0;
}

/*
// Implementation of a Length-Prefixed Framing Protocol
static int32_t one_request(int conn_fd) {
    // 1. Create the read buffer
    char rbuf[4 + k_max_msg];  // 4 bytes header

    // 2. Read exactly 4 bytes (the header)
    // errno is a Thread Local Gloabl Variable
    // If the kernel fails, it returns -1 to the C program and
    // writes the specific error code into the errno variable.
    errno = 0;
    int32_t err = read_full(conn_fd, rbuf, 4);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    // 3. Interpret the header as the length of the message
    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // 4. Ready exactly len bytes (the body)
    err = read_full(conn_fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // 5. Print the message
    // print a string of a specific length
    printf("client says: %.*s\n", len, &rbuf[4]);

    // 6. Prepare response
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];

    // 7. Write leangth header and body to the write buffer
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);

    // 8. Send the repsonse all at once
    return write_all(conn_fd, wbuf, 4 + len);
}

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
