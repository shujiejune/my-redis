#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h> // for offsetof
#include "avl.h"

#define container_of(ptr, type, member) ({                  \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type, member) );})

#define MAX(a, b) ((a) > (b) ? (a) : (b))

// --- C++ std::multiset Replacement ---

typedef struct {
    uint32_t *arr;
    size_t size;
    size_t cap;
} Multiset;

static void ms_init(Multiset *ms) {
    ms->arr = NULL;
    ms->size = 0;
    ms->cap = 0;
}

static void ms_insert(Multiset *ms, uint32_t val) {
    if (ms->size == ms->cap) {
        ms->cap = ms->cap ? ms->cap * 2 : 16;
        ms->arr = realloc(ms->arr, ms->cap * sizeof(uint32_t));
    }
    // Maintain sorted order
    size_t i = 0;
    while (i < ms->size && ms->arr[i] <= val) {
        i++;
    }
    memmove(&ms->arr[i + 1], &ms->arr[i], (ms->size - i) * sizeof(uint32_t));
    ms->arr[i] = val;
    ms->size++;
}

static bool ms_erase(Multiset *ms, uint32_t val) {
    for (size_t i = 0; i < ms->size; i++) {
        if (ms->arr[i] == val) {
            memmove(&ms->arr[i], &ms->arr[i + 1], (ms->size - i - 1) * sizeof(uint32_t));
            ms->size--;
            return true; // Erased one element
        }
    }
    return false;
}

static void ms_destroy(Multiset *ms) {
    free(ms->arr);
    ms->arr = NULL;
    ms->size = ms->cap = 0;
}

static bool ms_eq(Multiset *m1, Multiset *m2) {
    if (m1->size != m2->size) return false;
    return memcmp(m1->arr, m2->arr, m1->size * sizeof(uint32_t)) == 0;
}


// --- Core Data Structures ---

typedef struct Data {
    AVLNode node;
    uint32_t val;
} Data;

typedef struct Container {
    AVLNode *root;
} Container;


// --- AVL Operations ---

static void add(Container *c, uint32_t val) {
    Data *data = malloc(sizeof(Data));
    avl_init(&data->node);
    data->val = val;

    AVLNode *cur = NULL;
    AVLNode **from = &c->root;
    while (*from) {
        cur = *from;
        uint32_t node_val = container_of(cur, Data, node)->val;
        from = (val < node_val) ? &cur->left : &cur->right;
    }
    *from = &data->node;
    data->node.parent = cur;
    c->root = avl_fix(&data->node);
}

static bool del(Container *c, uint32_t val) {
    AVLNode *cur = c->root;
    while (cur) {
        uint32_t node_val = container_of(cur, Data, node)->val;
        if (val == node_val) {
            break;
        }
        cur = val < node_val ? cur->left : cur->right;
    }
    if (!cur) {
        return false;
    }

    c->root = avl_del(cur);
    free(container_of(cur, Data, node));
    return true;
}


// --- Testing Helpers ---

static void avl_verify(AVLNode *parent, AVLNode *node) {
    if (!node) {
        return;
    }

    assert(node->parent == parent);
    avl_verify(node, node->left);
    avl_verify(node, node->right);

    assert(node->cnt == 1 + avl_cnt(node->left) + avl_cnt(node->right));

    uint32_t l = avl_height(node->left);
    uint32_t r = avl_height(node->right);
    assert(l == r || l + 1 == r || l == r + 1);
    assert(node->height == 1 + MAX(l, r));

    uint32_t val = container_of(node, Data, node)->val;
    if (node->left) {
        assert(node->left->parent == node);
        assert(container_of(node->left, Data, node)->val <= val);
    }
    if (node->right) {
        assert(node->right->parent == node);
        assert(container_of(node->right, Data, node)->val >= val);
    }
}

static void extract(AVLNode *node, Multiset *extracted) {
    if (!node) {
        return;
    }
    extract(node->left, extracted);
    ms_insert(extracted, container_of(node, Data, node)->val);
    extract(node->right, extracted);
}

static void container_verify(Container *c, Multiset *ref) {
    avl_verify(NULL, c->root);
    assert(avl_cnt(c->root) == ref->size);

    Multiset extracted;
    ms_init(&extracted);
    extract(c->root, &extracted);
    assert(ms_eq(&extracted, ref));
    ms_destroy(&extracted);
}

static void dispose(Container *c) {
    while (c->root) {
        AVLNode *node = c->root;
        c->root = avl_del(c->root);
        free(container_of(node, Data, node));
    }
}


// --- Test Suites ---

static void test_insert(uint32_t sz) {
    for (uint32_t val = 0; val < sz; ++val) {
        Container c = {NULL};
        Multiset ref;
        ms_init(&ref);

        for (uint32_t i = 0; i < sz; ++i) {
            if (i == val) continue;
            add(&c, i);
            ms_insert(&ref, i);
        }
        container_verify(&c, &ref);

        add(&c, val);
        ms_insert(&ref, val);
        container_verify(&c, &ref);

        dispose(&c);
        ms_destroy(&ref);
    }
}

static void test_insert_dup(uint32_t sz) {
    for (uint32_t val = 0; val < sz; ++val) {
        Container c = {NULL};
        Multiset ref;
        ms_init(&ref);

        for (uint32_t i = 0; i < sz; ++i) {
            add(&c, i);
            ms_insert(&ref, i);
        }
        container_verify(&c, &ref);

        add(&c, val);
        ms_insert(&ref, val);
        container_verify(&c, &ref);

        dispose(&c);
        ms_destroy(&ref);
    }
}

static void test_remove(uint32_t sz) {
    for (uint32_t val = 0; val < sz; ++val) {
        Container c = {NULL};
        Multiset ref;
        ms_init(&ref);

        for (uint32_t i = 0; i < sz; ++i) {
            add(&c, i);
            ms_insert(&ref, i);
        }
        container_verify(&c, &ref);

        assert(del(&c, val));
        ms_erase(&ref, val);
        container_verify(&c, &ref);

        dispose(&c);
        ms_destroy(&ref);
    }
}


int main() {
    Container c = {NULL};
    Multiset ref;
    ms_init(&ref);

    // some quick tests
    container_verify(&c, &ref);
    add(&c, 123);
    ms_insert(&ref, 123);
    container_verify(&c, &ref);

    assert(!del(&c, 124));
    assert(del(&c, 123));
    ms_erase(&ref, 123);
    container_verify(&c, &ref);

    // sequential insertion
    for (uint32_t i = 0; i < 1000; i += 3) {
        add(&c, i);
        ms_insert(&ref, i);
        container_verify(&c, &ref);
    }

    // random insertion
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t val = (uint32_t)rand() % 1000;
        add(&c, val);
        ms_insert(&ref, val);
        container_verify(&c, &ref);
    }

    // random deletion
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t val = (uint32_t)rand() % 1000;
        bool in_ref = ms_erase(&ref, val);
        if (!in_ref) {
            assert(!del(&c, val));
        } else {
            assert(del(&c, val));
        }
        container_verify(&c, &ref);
    }

    // insertion/deletion at various positions
    for (uint32_t i = 0; i < 200; ++i) {
        test_insert(i);
        test_insert_dup(i);
        test_remove(i);
    }

    dispose(&c);
    ms_destroy(&ref);

    printf("All AVL tests passed successfully!\n");
    return 0;
}
