#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "hashtable.h"

const size_t k_rehashing_work = 128;
const size_t k_max_load_factor = 8;

static void h_init(HTable *htable, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);  // n must be a power of 2
    htable->table = (HNode **)calloc(n, sizeof(HNode *));
    htable->mask = n - 1;
    htable->size = 0;
}

// Insert a node into the hashtable
static void h_insert(HTable *htable, HNode *node) {
    size_t index = node->hcode & htable->mask;  // hcode % size
    node->next = htable->table[index];
    htable->table[index] = node;
    htable->size++;
}

// hashtable look up subroutine
// Return the address of the parent pointer that owns the target node
// which can be used to delete the target node
static HNode **h_lookup(HTable *htable, HNode *key, bool (*eq)(HNode *, HNode *)) {
    if (!htable->table) {
        return NULL;
    }

    size_t index = key->hcode & htable->mask;
    HNode **head = &htable->table[index];
    HNode *curr;
    while ((curr = *head) != NULL) {
        if (curr->hcode == key->hcode && eq(curr, key)) {
            return head;
        }
        head = &curr->next;
    }
    return NULL;
}

// Remove a node from the chain
static HNode *h_detach(HTable *htable, HNode **target) {
    // target is not a dummy pointer
    // it's the actual address of the next field inside the previous node
    HNode *node = *target;
    *target = node->next;   // update the incoming pointer to the next node
    htable->size--;
    return node;
}

// Scan each slot
// Move a constant number of nodes from the older table to the newer table
// Then exit
static void hm_help_rehashing(HMap *hmap) {
    size_t nwork = 0;
    while (nwork < k_rehashing_work && hmap->older.size > 0) {
        // find a non-empty slot
        HNode **target = &hmap->older.table[hmap->migrate_pos];
        if (!*target) {  // empty slot
            hmap->migrate_pos++;
            continue;
        }
        // move the first list node to the newer table
        h_insert(&hmap->newer, h_detach(&hmap->older, target));
        nwork++;
    }
    // discard the older table if migration isdone
    if (hmap->older.size == 0 && hmap->older.table) {
        free(hmap->older.table);
        memset(&hmap->older, 0, sizeof(HTable));  // explicit zeroing
    }
}

static void hm_trigger_rehashing(HMap *hmap) {
    assert(hmap->older.table == NULL);
    hmap->older = hmap->newer;
    h_init(&hmap->newer, (hmap->newer.mask + 1) * 2);
    hmap->migrate_pos = 0;
}

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
    hm_help_rehashing(hmap);
    HNode **head = h_lookup(&hmap->newer, key, eq);
    if (!head) {
        head = h_lookup(&hmap->older, key, eq);
    }
    return head ? *head : NULL;
}

void hm_insert(HMap *hmap, HNode *node) {
    if (!hmap->newer.table) {
        h_init(&hmap->newer, 4);  // initialize the newer table if empty
    }
    h_insert(&hmap->newer, node); // always insert into the newer table

    if (!hmap->older.table) {     // check if we need to rehash
        size_t shreshold = (hmap->newer.mask + 1) * k_max_load_factor;
        if (hmap->newer.size > shreshold) {
            hm_trigger_rehashing(hmap);
        }
    }
    hm_help_rehashing(hmap);      // migrate a small batch of nodes
}

HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
    hm_help_rehashing(hmap);
    HNode **target = h_lookup(&hmap->newer, key, eq);
    if (target) {
        return h_detach(&hmap->newer, target);
    }
    target = h_lookup(&hmap->older, key, eq);
    if (target) {
        return h_detach(&hmap->older, target);
    }
    return NULL;
}

void hm_clear(HMap *hmap) {
    free(hmap->newer.table);
    free(hmap->older.table);
    memset(hmap, 0, sizeof(HMap));
}

size_t hm_size(HMap *hmap) {
    return hmap->newer.size + hmap->older.size;
}
