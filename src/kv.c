#include "kv.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define container_of(ptr, T, member) \
    ((T *)( (char *)ptr - offsetof(T, member) ))

// Global head of the list
// static Entry *g_data = NULL;

// Global hashtable
static HMap g_data;

// FNV Hash
static uint64_t str_hash(const char *data) {
    uint32_t h = 0x811C9DC5;
    size_t len = strlen(data);
    for (size_t i =  0; i < len; i++) {
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}

// Check if two entries are equal
// called by hashtable
static bool entry_eq(HNode *lhs, HNode *rhs) {
    Entry *l = container_of(lhs, Entry, node);
    Entry *r = container_of(rhs, Entry, node);
    return strcmp(l->key, r->key) == 0;
}

// PUT: Insert or Update
void kv_put(const char *key, const char *val) {
    // Construct a "Dummy" Entry just for the lookup
    // We only need the key pointer and the calculated hash
    Entry key_dummy;
    key_dummy.key = (char *)key;
    key_dummy.node.hcode = str_hash(key);

    // Look it up
    HNode *node = hm_lookup(&g_data, &key_dummy.node, entry_eq);

    if (node) {
        // CASE A: Found! Update existing value.
        Entry *ent = container_of(node, Entry, node);
        free(ent->val);
        ent->val = strdup(val);
    } else {
        // CASE B: Not Found! Allocate and Insert.
        Entry *ent = malloc(sizeof(Entry));
        ent->key = strdup(key);
        ent->val = strdup(val);
        ent->node.hcode = key_dummy.node.hcode; // Copy the hash we already calculated
        ent->node.next = NULL;

        hm_insert(&g_data, &ent->node);
    }
}

// GET: Retrieve Value
char *kv_get(const char *key) {
    // Construct Dummy
    Entry key_dummy;
    key_dummy.key = (char *)key;
    key_dummy.node.hcode = str_hash(key);

    // Lookup
    HNode *node = hm_lookup(&g_data, &key_dummy.node, entry_eq);

    if (!node) {
        return NULL;
    }

    // Recover Entry and return value
    Entry *ent = container_of(node, Entry, node);
    return ent->val;
}

// DEL: Remove and Free
void kv_del(const char *key) {
    // Construct Dummy
    Entry key_dummy;
    key_dummy.key = (char *)key;
    key_dummy.node.hcode = str_hash(key);

    // Delete (removes from list, returns the node)
    HNode *node = hm_delete(&g_data, &key_dummy.node, entry_eq);

    if (node) {
        // If it existed, we must free the memory!
        Entry *ent = container_of(node, Entry, node);
        free(ent->key);
        free(ent->val);
        free(ent);
    }
}
