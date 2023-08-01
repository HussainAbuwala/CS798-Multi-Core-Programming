/**
 * Implementation of the relaxed AVL tree of Bronson et al.,
 * which uses optimistic concurrency control and fine grained locking.
 *
 * This is based on Philip W. Howard's code
 * (but converted to a class and templated/genericized,
 *  and with proper memory reclamation).
 *
 * Trevor Brown, 2018.
 */

#ifndef BRONSON_PEXT_BST_OCC_ADAPTER_H
#define BRONSON_PEXT_BST_OCC_ADAPTER_H

#define USE_TREE_STATS

#include <iostream>
#include "common/plaf.h"
#include "common/errors.h"
#include "common/recordmgr/record_manager.h"
#ifdef USE_TREE_STATS
#   define TREE_STATS_BYTES_AT_DEPTH
#   include "tree_stats.h"
#endif
#include "ccavl_impl.h"

#define NODE_T node_t<K,V>
#define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, NODE_T>
#define DATA_STRUCTURE_T ccavl<K, V, RECORD_MANAGER_T>

template <typename K, typename V, class Reclaim = reclaimer_debra<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class OCCBST {
private:
    DATA_STRUCTURE_T * const tree;
    K _minKey;
    K _maxKey;

public:
    OCCBST(const int NUM_THREADS,
               const K& minKey,
               const K& maxKey)
        : tree(new DATA_STRUCTURE_T(NUM_THREADS, minKey))
        , _minKey(minKey)
        , _maxKey(maxKey)
    {
        if (NUM_THREADS > MAX_THREADS_POW2) {
            setbench_error("NUM_THREADS exceeds MAX_THREADS_POW2");
        }
    }
    ~OCCBST() {
        delete tree;
    }

    V getNoValue() {
        return (V) NULL;
    }

    // void initThread(const int tid) {
    //     tree->initThread(tid);
    // }
    // void deinitThread(const int tid) {
    //     tree->deinitThread(tid);
    // }

    bool contains(const int tid, const K& key) {
        tree->initThread(tid);
        return tree->find(tid, key) != getNoValue();
    }
    // V insert(const int tid, const K& key, const V& val) {
    //     tree->initThread(tid);
    //     return tree->insertReplace(tid, key, val);
    // }
    bool insertIfAbsent(const int tid, const K& key) {
        tree->initThread(tid);
        return tree->insertIfAbsent(tid, key, (V) (&key)) == getNoValue();
    }
    bool erase(const int tid, const K& key) {
        tree->initThread(tid);
        return tree->erase(tid, key) != getNoValue();
    }
    V find(const int tid, const K& key) {
        tree->initThread(tid);
        return tree->find(tid, key);
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        setbench_error("rangeQuery not implemented for this data structure");
    }
    void printSummary() {
        tree->printSummary();
    }
    bool validateStructure() {
        return true;
    }
    void printObjectSizes() {
        std::cout<<"sizes: node="
                 <<(sizeof(NODE_T))
                 <<std::endl;
    }
    // try to clean up: must only be called by a single thread as part of the test harness!
    void debugGCSingleThreaded() {
        tree->debugGetRecMgr()->debugGCSingleThreaded();
    }

#ifdef USE_TREE_STATS
    class NodeHandler {
    public:
        typedef NODE_T * NodePtrType;
        K minKey;
        K maxKey;

        NodeHandler(const K& _minKey, const K& _maxKey) {
            minKey = _minKey;
            maxKey = _maxKey;
        }

        class ChildIterator {
        private:
            bool leftDone;
            bool rightDone;
            NodePtrType node; // node being iterated over
        public:
            ChildIterator(NodePtrType _node) {
                node = _node;
                leftDone = (node->left == NULL);
                rightDone = (node->right == NULL);
            }
            bool hasNext() {
                return !(leftDone && rightDone);
            }
            NodePtrType next() {
                if (!leftDone) {
                    leftDone = true;
                    return node->left;
                }
                if (!rightDone) {
                    rightDone = true;
                    return node->right;
                }
                setbench_error("ERROR: it is suspected that you are calling ChildIterator::next() without first verifying that it hasNext()");
            }
        };

        bool isLeaf(NodePtrType node) {
            return (node->left == NULL) && (node->right == NULL);
        }
        size_t getNumChildren(NodePtrType node) {
            return (node->left != NULL) + (node->right != NULL);
        }
        size_t getNumKeys(NodePtrType node) {
            if (node->value == NULL) return 0;
            if (node->key == minKey || node->key == maxKey) return 0;
            return 1;
        }
        size_t getSumOfKeys(NodePtrType node) {
            if (getNumKeys(node) == 0) return 0;
            return (size_t) node->key;
        }
        ChildIterator getChildIterator(NodePtrType node) {
            return ChildIterator(node);
        }
        static size_t getSizeInBytes(NodePtrType node) { return sizeof(*node); }
    };
    TreeStats<NodeHandler> * createTreeStats(const K& _minKey, const K& _maxKey) {
        return new TreeStats<NodeHandler>(new NodeHandler(_minKey, _maxKey), tree->get_root(), true);
    }
#endif

    long getSumOfKeys() {
        auto treeStats = createTreeStats(_minKey, _maxKey);
        long sum = treeStats->getSumOfKeys();
        // std::cout<<treeStats->toString();
        return sum;
    }

    void printDebuggingDetails() {

    }

private:
    template<typename... Arguments>
    void iterate_helper_fn(int depth, void (*callback)(K key, V value, Arguments... args)
            , NODE_T * const curr, Arguments... args) {
        if (curr == NULL) return;
        if (depth == 10) {
            #pragma omp task
            iterate_helper_fn(1+depth, callback, curr->left, args...);
            #pragma omp task
            iterate_helper_fn(1+depth, callback, curr->right, args...);
        } else {
            iterate_helper_fn(1+depth, callback, curr->left, args...);
            iterate_helper_fn(1+depth, callback, curr->right, args...);
        }
        callback(curr->key, curr->value, args...);
    }

public:
    #define OCCBST_SUPPORTS_TERMINAL_ITERATE
    template<typename... Arguments>
    void iterate(void (*callback)(K key, V value, Arguments... args), Arguments... args) {
        #pragma omp parallel
        {
            #pragma omp single
            iterate_helper_fn(0, callback, tree->get_root(), args...);
        }
    }

};

#undef RECORD_MANAGER_T
#undef DATA_STRUCTURE_T

#endif
