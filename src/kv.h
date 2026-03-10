#ifndef KV_H
#define KV_H

#include <stdio.h>
#include <stdbool.h>
#include "hashtable.h"

// Key-Value Store (simply linked list)
typedef struct Entry {
    HNode node;  // intrusive hashtable hook
    char *key;
    char *val;
} Entry;

size_t kv_size(void);
void kv_put(const char *key, const char *val);
char *kv_get(const char *key);
bool kv_del(const char *key);
void kv_foreach(bool (*cb)(const char *key, void *arg), void *arg);

#endif
