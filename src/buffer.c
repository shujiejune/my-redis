#include "buffer.h"
#include "common.h"
#include <stdlib.h>
#include<string.h>

void buffer_init(Buffer *buf, size_t capacity) {
    buf->data = malloc(capacity);
    buf->capacity = capacity;
    buf->r_pos = 0;
    buf->w_pos = 0;
}

void buffer_destroy(Buffer *buf) {
    free(buf->data);
}

// Ensure there is anough space for appending data
void buf_reserve(Buffer *buf, size_t n) {
    // Case A: already have enough space at the end. Do nothing.
    if (buf_write_space(buf) >= n) {
        return;
    }

    // Case B: not enough space at the end. Compact (slide data).
    size_t total_free_space = buf->capacity - buf_read_size(buf);
    if (total_free_space >= n) {
        // We have enough space, but it's fragmented
        memmove(buf->data, buf_read_ptr(buf), buf_read_size(buf));
        buf->r_pos = 0;
        buf->w_pos = buf_read_size(buf);
    } else {
        // Buffer is too small, need to allocate more memory
        size_t new_capacity = buf->capacity + n;
        uint8_t *new_data = realloc(buf->data, new_capacity);
        if (!new_data) {
            die("Memory allocation failed");
        }
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
}

// O(1) operation: append data by reserving space and potentially compacting if needed
void buf_append(Buffer *buf, const uint8_t *data, size_t len) {
    // Ensure we have enough space for the data
    buf_reserve(buf, len);
    // Append data to the safe space
    memcpy(buf_write_ptr(buf), data, len);
    // Advance the write pointer
    buf->w_pos += len;
}

// O(1) operation: consume data by advancing the read pointer
void buf_consume(Buffer *buf, size_t n) {
    if (buf->r_pos + n > buf->w_pos) {
        // Handle invalid consumption
        die("Invalid consumption");
    }
    buf->r_pos += n;
    // If buffer is empty, reset read pointer to start
    if (buf->r_pos == buf->w_pos) {
        buf->r_pos = 0;
        buf->w_pos = 0;
    }
}

// Return pointer to active data
uint8_t *buf_read_ptr(Buffer *buf) {
    return &buf->data[buf->r_pos];
}

// Return how many bytes of valid data we have
size_t buf_read_space(Buffer *buf) {
    return buf->w_pos - buf->r_pos;
}

// Return pointer to free space for writing
uint8_t *buf_write_ptr(Buffer *buf) {
    return &buf->data[buf->w_pos];
}

// Return how many bytes of free space is left
size_t buf_write_space(Buffer *buf) {
    return buf->capacity - buf->w_pos;
}
