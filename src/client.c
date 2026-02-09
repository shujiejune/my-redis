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
static int32_t send_req(int fd, const char **cmd, size_t n_cmd);
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

    // const char *query_list[] = {"hello1", "hello2", "pipelined", long_msg, "test"};
    size_t query_count = 7;

    // --- PIPELINING STEP 1: SEND EVERYTHING ---
    printf("--- Sending requests ---\n");
    const char *cmd_set[] ={"set", "mykey", "123"};
    int32_t err = send_req(fd, cmd_set, 3);
    if (err) goto L_DONE;
    const char *cmd_get[] = {"get", "mykey"};
    err = send_req(fd, cmd_get, 2);
    if (err) goto L_DONE;
    const char *cmd_del[] = {"del", "mykey"};
    err = send_req(fd, cmd_del, 2);
    if (err) goto L_DONE;

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

static int32_t send_req(int fd, const char **cmd, size_t n_cmd) {
    char wbuf[4 + k_max_msg];
    char *ptr = wbuf + 4;

    // 1. Write number of strings (nstr)
    uint32_t n = (uint32_t)n_cmd;
    memcpy(ptr, &n, 4);
    ptr += 4;

    // 2. Write each length-prefixed string [len][data]
    for (size_t i = 0; i < n_cmd; i++) {
        uint32_t len = (uint32_t)strlen(cmd[i]);
        memcpy(ptr, &len, 4);
        ptr += 4;
        memcpy(ptr, cmd[i], len);
        ptr += len;
    }

    // 3. Write header
    uint32_t total_len = (uint32_t)(ptr - wbuf - 4);
    memcpy(wbuf, &total_len, 4);

    return write_all(fd, wbuf, 4 + total_len);
}

static int32_t read_res(int fd) {
    char rbuf[4 + k_max_msg + 1];  // +1 for null terminator
    errno = 0;

    // Read header
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    // Parse length
    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    if (len > k_max_msg) {
        msg("response too long");
        return -1;
    }

    // Read body (status + message)
    /*
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    printf("Server says: %.*s\n", len, &rbuf[4]);
    */
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    uint32_t status = 0;
    memcpy(&status, &rbuf[4], 4);
    if (status != 0) {
        // Successfully received an error response
        printf("Error: Server returned status %d. Msg: %.*s\n", status, len - 4, &rbuf[8]);
    } else {
        printf("Server says: %.*s\n", len - 4, &rbuf[8]);
    }

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
