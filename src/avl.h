#ifndef AVL_H
#define AVL_H

#include <stddef.h>
#include <stdint.h>

typedef struct AVLNode AVLNode;

struct AVLNode {
    AVLNode *parent;
    AVLNode *left;
    AVLNode *right;
    uint32_t height;  // subtree height
    uint32_t cnt;     // subtree size
};

// For tiny functions, jumping around in memory to look for the function
// takes longer than doing a simple initialization or calculation inline (copy and paste)
inline void avl_init(AVLNode *node) {
    node->left = node->right = node->parent = NULL;
    node->height = 1;
    node->cnt = 1;
}

inline uint32_t avl_height(AVLNode *node) {
    return node ? node->height : 0;
}

inline uint32_t avl_cnt(AVLNode *node) {
    return node ? node->cnt : 0;
}

AVLNode *avl_fix(AVLNode *node);
AVLNode *avl_del(AVLNode *node);

#endif
