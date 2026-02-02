#ifndef COMMON_H  // prevent importing this file twice
#define COMMON_H

#include <stddef.h>  // for size_t
#include <stdint.h>  // for int32_t
#include <wchar.h>

void msg(const char *msg);
void die(const char *msg);
void fd_set_nb(int fd);
int32_t read_full(int fd, char *buf, size_t n);
int32_t write_all(int fd, char *buf, size_t n);

#endif
