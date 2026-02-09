#ifndef KV_H
#define KV_H

#include <stdio.h>

// Key-Value Store (simply linked list)
typedef struct Entry {
    struct Entry *next;
    char *key;
    char *val;
} Entry;

void kv_put(const char *key, const char *val);
char *kv_get(const char *key);
void kv_del(const char *key);

#endif
