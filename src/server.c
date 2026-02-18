#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common.h"
#include "buffer.h"
#include "kv.h"
#include "hashtable.h"

#define k_max_msg 4096
#define k_max_args 200 * 1000

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

enum {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2
};

// Context of a connection
typedef struct Conn {
    int fd;
    int state;  // STATE_REQ or STATE_RES
    // rbuf and wbuf are in the userspace (heap memory), not in the kernel
    /*
    // read buffer
    size_t rbuf_size;  // how many bytes currently in rbuf
    uint8_t rbuf[4 + k_max_msg];
    // write buffer
    size_t wbuf_size;  // total bytes to send
    size_t wbuf_sent;  // offset (already sent)
    uint8_t wbuf[4 + k_max_msg];
    */
    Buffer rbuf;
    Buffer wbuf;
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
    /*
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    */
    buffer_init(&conn->rbuf, k_max_msg);
    buffer_init(&conn->wbuf, k_max_msg);

    conn_put(conn);
    return 0;
}

static bool read_u32(const uint8_t **curr, const uint8_t *end, uint32_t *out) {
    if (*curr + 4 > end) {
        return false;
    }
    memcpy(out, *curr, 4);
    *curr += 4;
    return true;
}

static bool read_str(const uint8_t **curr, const uint8_t *end, char **out) {
    uint32_t len = 0;
    if (!read_u32(curr, end, &len)) {
        return false;
    }
    if (*curr + len > end) {
        return false;
    }
    *out = (char *)malloc(len + 1);
    if (!*out) {
        return false;
    }
    memcpy(*out, *curr, len);
    (*out)[len] = '\0';
    *curr += len;
    return true;
}

// --- Command Execution ---

static void do_get(char **cmd, Buffer *out) {
    // Call the logic layer
    char *val = kv_get(cmd[1]);

    // Format the network response
    uint32_t status = RES_OK;
    if (!val) {
        status = RES_NX;
    }

    // Response format: [TotalLen][Status][Value]
    uint32_t val_len = val ? (uint32_t)strlen(val) : 0;
    uint32_t total_len = 4 + val_len;

    buf_append(out, (uint8_t *)&total_len, 4);
    buf_append(out, (uint8_t *)&status, 4);
    if (val) {
        buf_append(out, (uint8_t *)val, val_len);
    }
}

static void do_set(char **cmd, Buffer *out) {
    kv_put(cmd[1], cmd[2]);

    // Response: OK
    uint32_t status = RES_OK;
    uint32_t total_len = 4; // Just status code

    buf_append(out, (uint8_t *)&total_len, 4);
    buf_append(out, (uint8_t *)&status, 4);
}

static void do_delete(char **cmd, Buffer *out) {
    kv_del(cmd[1]);

    // Response: OK
    uint32_t status = RES_OK;
    uint32_t total_len = 4;

    buf_append(out, (uint8_t *)&total_len, 4);
    buf_append(out, (uint8_t *)&status, 4);
}

static void do_request(char **cmd, size_t n_cmd, Buffer *wbuf) {
    if (n_cmd == 2 && strcmp(cmd[0], "get") == 0) {
        do_get(cmd, wbuf);
    } else if (n_cmd == 3 && strcmp(cmd[0], "set") == 0) {
        do_set(cmd, wbuf);
    } else if (n_cmd == 2 && strcmp(cmd[0], "del") == 0) {
        do_delete(cmd, wbuf);
    } else {
        uint32_t status = RES_ERR;
        char *msg = "Unknown command";
        uint32_t msg_len = strlen(msg);
        uint32_t total_len = 4 + msg_len;

        buf_append(wbuf, (uint8_t *)&total_len, 4);
        buf_append(wbuf, (uint8_t *)&status, 4);
        buf_append(wbuf, (uint8_t *)msg, msg_len);
    }
}

// Main parsing loop
static ReqStatus try_one_request(Conn *conn) {
    Buffer *rbuf = &conn->rbuf;
    Buffer *wbuf = &conn->wbuf;

    // 1. Check for the 4-byte header
    if (buf_read_size(rbuf) < 4) {
        return REQ_INCOMPLETE;
    }

    uint32_t len = 0;
    memcpy(&len, buf_read_ptr(rbuf), 4);
    if (len > k_max_msg) {
        msg("too long");
        conn->state = STATE_END;
        return REQ_ERROR;
    }
    // Do not proceed to parse until we read the full message
    if (4 + len > buf_read_size(rbuf)) {
        return REQ_INCOMPLETE;  // wait for more data
    }

    // 2. Parse payload
    const uint8_t *curr = buf_read_ptr(rbuf) + 4;
    const uint8_t *end = curr + len;

    uint32_t n_cmd = 0;
    if (!read_u32(&curr, end, &n_cmd)) {
        conn->state = STATE_END;
        return REQ_ERROR;
    }
    if (n_cmd > 16) {  // dafety limit on args
        conn->state = STATE_END;
        return REQ_ERROR;
    }

    // Parse list of strings
    char *cmd[16];
    for (uint32_t i = 0; i < n_cmd; i++) {
        if (!read_str(&curr, end, &cmd[i])) {
            // Cleanup already parsed strings on error
            for (uint32_t j = 0; j < i; j++) {
                free(cmd[j]);
            }
            conn->state = STATE_END;
            return REQ_ERROR;
        }
    }

    // 3. Got a full message
    printf("client says: %.*s\n", len, buf_read_ptr(rbuf) + 4);

    // 4. Execute command (Generate response)
    /*
    const char *reply = "world";
    uint32_t reply_len = (uint32_t)strlen(reply);

    memcpy(&conn->wbuf[0], &reply_len, 4);
    memcpy(&conn->wbuf[4], reply, reply_len);
    conn->wbuf_size = 4 + reply_len;
    conn->wbuf_sent = 0;

    buf_append(wbuf, (const uint8_t *)&reply_len, 4);
    buf_append(wbuf, (const uint8_t *)reply, reply_len);

    // 5. Remove the request from rbuf (left shift)
    size_t remain = conn->rbuf_size - (4 + len);
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    // 6. Change connection state to response
    conn->state = STATE_RES;

    // Commit the write
    wbuf->w_pos += (4 + reply_len);
    */
    do_request(cmd, n_cmd, &conn->wbuf);

    // 5. Cleanup
    for (uint32_t i = 0; i < n_cmd; i++) {
        free(cmd[i]);
    }

    // Consume request from rbuf
    buf_consume(rbuf, 4 + len);

    return REQ_PROCESSED;
}

static void handle_read(Conn *conn) {
    /*
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], sizeof(conn->rbuf) - conn->rbuf_size);
    */
    buf_reserve(&conn->rbuf, 1024);
    ssize_t rv = read(conn->fd, buf_write_ptr(&conn->rbuf), buf_write_space(&conn->rbuf));

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

    // Mark bytes as written
    // conn->rbuf_size += (size_t)rv;
    conn->rbuf.w_pos += (size_t)rv;

    // Pipelining loop
    // While there is enough data for a full request, keep processing.
    while (try_one_request(conn) == REQ_PROCESSED) {
        /*
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
        */
    }

    // If we have data in wbuf, we want to write it out
    if (buf_read_size(&conn->wbuf) > 0) {
        conn->state = STATE_RES;
    }
}

static void handle_write(Conn *conn) {
    /*
    assert(conn->wbuf_size > conn->wbuf_sent);
    ssize_t rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], conn->wbuf_size - conn->wbuf_sent);
    */
    ssize_t rv = write(conn->fd, buf_read_ptr(&conn->wbuf), buf_read_size(&conn->wbuf));

    if (rv <= 0) {
        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {  // Not ready, try again later.
            return;
        }
        conn->state = STATE_END;
        return;
    }

    /*
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    */
    buf_consume(&conn->wbuf, (size_t)rv);

    // If finished sending the whole response, switch back to the reading mode
    if (buf_read_size(&conn->wbuf) == 0) {
        conn->state = STATE_REQ;
        conn->wbuf.r_pos = 0;
        conn->wbuf.w_pos = 0;
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
