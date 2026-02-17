#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Forward declaration because HNode is self-referential
struct HNode;

// hashtable node, should be embedded in the payload
typedef struct HNode {
    struct HNode *next;
    uint64_t hcode;
} HNode;

// a simple fixed-sized hashtable
// default values do not exist in C
// thus we do not initialize them here
typedef struct HTable {
    HNode **table;     // array of slots
    size_t mask;       // 2 ^ n - 1
    size_t size;       // number of keys, n
} HTable;

// real hashtable interface
// uses 2 hashtables for progressive rehashing
typedef struct HMap {
    HTable newer;
    HTable older;
    // move a small batch from older to newer every time
    // mark the position of the last migrated node
    size_t migrate_pos;
} HMap;

// *eq is a function pointer to a function that compares two HNode pointers
// HMap is generic and doesn't know about the data of the HNode
// eq takes two HNodes, finds their parent data using container_of and compares their actual keys
HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *key);
HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void hm_clear(HMap *hmap);
size_t hm_size(HMap *hmap);

#endif
