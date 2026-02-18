#ifndef KV_H
#define KV_H

#include <stdio.h>
#include "hashtable.h"


// Key-Value Store (simply linked list)
typedef struct Entry {
    HNode node;  // intrusive hashtable hook
    char *key;
    char *val;
} Entry;

Entry *kv_find(const char *key);
void kv_put(const char *key, const char *val);
char *kv_get(const char *key);
void kv_del(const char *key);

#endif
