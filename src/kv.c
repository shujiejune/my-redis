#include "kv.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global head of the list
static Entry *g_data = NULL;

Entry *kv_find(const char *key) {
    Entry *e = g_data;
    while (e) {
        if (strcmp(e->key, key) == 0) {
            return e;
        }
        e = e->next;
    }
    return NULL;
}

void kv_put(const char *key, const char *val) {
    Entry *e = kv_find(key);
    if (e) {
        free(e->val);
        e->val = strdup(val);
    } else {
        Entry *new_entry = malloc(sizeof(Entry));
        if (!new_entry) {
            fprintf(stderr, "Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
        new_entry->key = strdup(key);
        new_entry->val = strdup(val);
        new_entry->next = g_data;
        g_data = new_entry;
    }
}

char *kv_get(const char *key) {
    Entry *entry = kv_find(key);
    return entry ? entry->val : NULL;
}

void kv_del(const char *key) {
    Entry **pp = &g_data;
    while (*pp) {
        Entry *e = *pp;
        if (strcmp(e->key, key) == 0) {
            *pp = e->next;
            free(e->key);
            free(e->val);
            free(e);
            return;
        }
        pp = &e->next;
    }
}
