#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common.h"

#define k_max_msg 4096

static int32_t query(int fd, const char *text);
static int32_t send_req(int fd, const char *text);
static int32_t read_res(int fd);

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket creation failed");
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6379);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1, defined in host byte order
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv < 0) {
        die("Connect failed");
    }

    // Multiple pipelined requests
    char *long_msg = malloc(k_max_msg + 1);
    if (!long_msg) {
        die("malloc failed");
    }
    memset(long_msg, 'z', k_max_msg);
    long_msg[k_max_msg] = '\0';

    const char *query_list[] = {"hello1", "hello2", "pipelined", long_msg, "test"};
    size_t query_count = 5;

    // --- PIPELINING STEP 1: SEND EVERYTHING ---
    printf("--- Sending %d requests ---\n", (int)query_count);
    for (int i = 0; i < query_count; i++) {
        int32_t err = send_req(fd, query_list[i]);
        if (err) goto L_DONE;
    }

    // --- PIPELINING STEP 2: READ EVERYTHING ---
    printf("--- Waiting for responses ---\n");
    for (int i = 0; i < query_count; i++) {
        int32_t err = read_res(fd);
        if (err) goto L_DONE;
    }

L_DONE:
    close(fd);
    free(long_msg);
    return 0;
}

static int32_t send_req(int fd, const char *text) {
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg) {
        return-1;
    }

    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);
    return write_all(fd, wbuf, 4 + len);
}

static int32_t read_res(int fd) {
    char rbuf[4 + k_max_msg];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    if (len > k_max_msg) {
        msg("response too long");
        return -1;
    }

    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    printf("Server says: %.*s\n", len, &rbuf[4]);
    return 0;
}

static int32_t query(int fd, const char *text) {
    /* 1. Get the length of request */
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg) {
        return -1;
    }

    /* 2. Send request */
    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);
    int32_t err = write_all(fd, wbuf, 4 + len);
    if (err) {
        return err;
    }

    /* 3. Wait for the response */
    char rbuf[4 + k_max_msg];
    errno = 0;
    err = read_full(fd, rbuf, 4);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }
    memcpy(&len, rbuf, 4);
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    /* 4. Print the response */
    printf("Server says: %.*s\n", len, &rbuf[4]);

    return 0;
}
