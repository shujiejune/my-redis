#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

void msg(const char *msg) {
    // printf is buffered, writes to stdout, i.e. the result of the program.
    // fprintf is unbuffered (appears immediately), writes to stderr, separated from the data stream.
    fprintf(stderr, "%s\n", msg);
}

void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    // exit(1): quit. It cleans up, flushes buffers, and closes files gracefully.
    // abort(): crash. It raises the SIGABRT signal, forces the OS to dump a
    // snapshot of memory at the crashing moment, which can be opened in GDB later.
    abort();
}

int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        // size_t is non-negative (0 - MAX_INT), used for counting.
        // ssize_t is signed, used when a function needs to return a count or an error.
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error or unexpected EOF
        }
        // sanity check for things that should be impossible.
        // If read() returns MORE bytes than n, crash immediately.
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

int32_t write_all(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error or unexpected EOF
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}
