#ifndef __RANGETREE_HH__
#define __RANGETREE_HH__

#include <iostream>
#include <sstream>

#include <malloc.h>

#include "libmallocprof.h"
#include "real.hh"
#include "spinlock.hh"

template <typename T>
class RangeTree {
    private:
        static const short BLACK = 0;
        static const short RED = 1;

        struct node_t {
            short color;
            size_t startRange;
            size_t endRange;
            node_t *left;
            node_t *right;
            T data;

            node_t(size_t start, size_t end) : startRange(start), endRange(end) {}
        };

        node_t *createNode(size_t start, size_t end, const T& object) {
            node_t *newNode = (node_t *)myMalloc(sizeof(node_t));
            newNode->left = nullptr;
            newNode->right = nullptr;
            newNode->data = object;
            newNode->startRange = start;
            newNode->endRange = end;
            newNode->color = BLACK;
            return newNode;
        }

        inline bool isChild(node_t *node) {
            return node->left == nullptr && node->right == nullptr;
        }

        node_t *root;

    public:
        RangeTree() : root(nullptr) {}

        void insert(size_t start, size_t end, const T object) {
            node_t *newNode = nullptr;
            if (root == nullptr) {
                root = createNode(start, end, object);
            } else {
                newNode = createNode(start, end, object);

                // find place to insert node into
                node_t *tracker = root;
                while (!isChild(tracker)) { // Till we hit a leaf node
                    if (tracker->startRange >= newNode->startRange) {
                        if (tracker->left == nullptr)
                            break;
                        tracker = tracker->left;
                    }
                    else {
                        if (tracker->right == nullptr)
                            break;
                        tracker = tracker->right;
                    }
                }

                // Insert into
                if (tracker->startRange >= newNode->startRange)
                    tracker->left = newNode;
                else
                    tracker->right = newNode;
            }
        }

        T& remove(size_t key) {}

        void find(size_t key, T* item) {
            node_t *tracker = root;
            while (tracker != nullptr) {
                if (tracker->startRange <= key && key <= tracker->endRange) {
                    *item = tracker->data;
                    break;
                }
                if (tracker->startRange >= key)
                    tracker = tracker->left;
                else
                    tracker = tracker->right;
            }
        }

        friend std::string getAllFrom(node_t *node, std::stringstream &ss) {
            if (node == nullptr)
                return ss.str();
            ss << getAllFrom(node->right, ss);
            ss << "Obj(" << node->data << ")" << ": range<" << node->startRange << "," << node->endRange << ">" << std::endl;
            ss << getAllFrom(node->left, ss);
            return ss.str();
        }

        friend std::ostream &operator<<(std::ostream &os, const RangeTree &rhs) {
            std::stringstream ss;
            // node_t *tracker = rhs.root;
            ss << getAllFrom(rhs.root, ss);
            // ss << "Obj(" << tracker->data << ")" << ": range<" << tracker->startRange << "," << tracker->endRange << ">" << std::endl;
            // ss << getAllFrom(tracker->right, ss);
            os << ss.str();
            return os;
        }
};

#endif /* end of include guard: __RANGETREE_HH__ */
