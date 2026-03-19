#include <assert.h>
#include "avl.h"

static uint32_t max(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static void avl_update(AVLNode *node) {
    node->height = 1 + max(avl_height(node->left), avl_height(node->right));
    node->cnt = 1 + (node->left ? node->left->cnt : 0) + (node->right ? node->right->cnt : 0);
}

static AVLNode *rotate_left(AVLNode *node) {
    AVLNode *parent = node->parent;
    AVLNode *new_node = node->right;
    AVLNode *inner = new_node->left;
    // Build the link between node and inner
    node->right = inner;
    if (inner) {
        inner->parent = node;
    }
    // Build the link between new_node and parent
    new_node->parent = parent;
    // Build the link between new_node and node
    new_node->left = node;
    node->parent = new_node;
    // Update heights and counts
    avl_update(node);
    avl_update(new_node);
    return new_node;
}

static AVLNode *rotate_right(AVLNode *node) {
    AVLNode *parent = node->parent;
    AVLNode *new_node = node->left;
    AVLNode *inner = new_node->right;
    // Build the link between node and inner
    node->left = inner;
    if (inner) {
        inner->parent = node;
    }
    // Build the link between new_node and parent
    new_node->parent = parent;
    // Build the link between new_node and node
    new_node->right = node;
    node->parent = new_node;
    // Update heights and counts
    avl_update(node);
    avl_update(new_node);
    return new_node;
}

// Case: the left subtree is taller by 2
static AVLNode *avl_fix_left(AVLNode *node) {
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = rotate_left(node->left);
    }
    return rotate_right(node);
}

// Case: the right subtree is taller by 2
static AVLNode *avl_fix_right(AVLNode *node) {
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right = rotate_right(node->right);
    }
    return rotate_left(node);
}

// Fix the AVL tree after an insertion or deletion
AVLNode *avl_fix(AVLNode *node) {
    while (1) {
        AVLNode **from = &node;  // Save the subtree
        AVLNode *parent = node->parent;
        if (parent) {
            // Attach the subtree to the parent
            from = parent->left == node ? &parent->left : &parent->right;
        }

        avl_update(node);

        // Fix the height difference of 2
        uint32_t l = avl_height(node->left);
        uint32_t r = avl_height(node->right);
        if (l - r == 2) {
            *from = avl_fix_left(node);
        } else if (r - l == 2) {
            *from = avl_fix_right(node);
        }

        // node is root
        if (!parent) {
            return *from;
        }

        // Continue to the parent node because its height may be changed
        node = parent;
    }
}

// Easy deletion: detach a node with at most one child
static AVLNode *avl_del_easy(AVLNode *node) {
    assert(!node->left || !node->right);  // Node has at most one child
    AVLNode *child = node->left ? node->left : node->right;
    AVLNode *parent = node->parent;
    if (child) {
        child->parent = node->parent;
    }
    if (!parent) {
        return child;  // Remove the root node
    }
    AVLNode **from = parent->left == node ? &parent->left : &parent->right;
    *from = child;
    return avl_fix(parent);
}

AVLNode *avl_del(AVLNode *node) {
    if (!node->left || !node->right) {
        return avl_del_easy(node);
    }
    // Find the successor (leftmost node in the right subtree)
    AVLNode *successor = node->right;
    while (successor->left) {
        successor = successor->left;
    }
    // Detach the successor from its parent
    AVLNode *root = avl_del_easy(successor);
    // Swap node with the successor
    *successor = *node;  // Update left, right, parent
    if (successor->left) {
        successor->left->parent = successor;
    }
    if (successor->right) {
        successor->right->parent = successor;
    }
    // Attach the successor to the parent, or update the root pointer
    AVLNode **from = &root;
    AVLNode *parent = node->parent;
    if (parent) {
        from = parent->left == node ? &parent->left : &parent->right;
    }
    *from = successor;
    return root;
}
