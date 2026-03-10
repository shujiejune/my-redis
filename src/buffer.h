#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>
#include <stddef.h>

typedef struct Buffer {
    uint8_t *data;   // points to the beginning of the buffer
    size_t capacity; // total capacity of the buffer, e.g. k_max_msg
    size_t r_pos;    // read position
    size_t w_pos;    // write position
} Buffer;

void buffer_init(Buffer *buf, size_t capacity);
void buffer_destroy(Buffer *buf);
void buf_reserve(Buffer *buf, size_t n);
void buf_append(Buffer *buf, const uint8_t *data, size_t len);
void buf_consume(Buffer *buf, size_t n);

void buf_append_u8(Buffer *buf, uint8_t data);
void buf_append_u32(Buffer *buf, uint32_t data);
void buf_append_i64(Buffer *buf, int64_t data);

uint8_t *buf_write_ptr(Buffer *buf);
size_t buf_write_space(Buffer *buf);
uint8_t *buf_read_ptr(Buffer *buf);
size_t buf_read_size(Buffer *buf);

#endif
