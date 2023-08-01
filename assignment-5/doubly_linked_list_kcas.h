#pragma once

#include <cassert>
#include "kcas.h"

struct Node{
        casword_t prev;
        casword_t next;
        int val;
        casword_t mark;
};

class DoublyLinkedList {
private:
    volatile char padding0[PADDING_BYTES];
    const int numThreads;
    const int minKey;
    const int maxKey;
    volatile char padding1[PADDING_BYTES];

    Node * head;
    Node * tail;
    KCASLockFree<5> kcas;
    

public:
    DoublyLinkedList(const int _numThreads, const int _minKey, const int _maxKey);
    ~DoublyLinkedList();
    
    void initNode(const int tid, Node * node, casword_t prev, casword_t next, int val, casword_t mark);
    pair<Node *, Node *> internalSearch(const int tid, const int & key);

    bool contains(const int tid, const int & key);
    bool insertIfAbsent(const int tid, const int & key); // try to insert key; return true if successful (if it doesn't already exist), false otherwise
    bool erase(const int tid, const int & key); // try to erase key; return true if successful, false otherwise
    
    long getSumOfKeys(); // should return the sum of all keys in the set
    void printDebuggingDetails(); // print any debugging details you want at the end of a trial in this function
};

DoublyLinkedList::DoublyLinkedList(const int _numThreads, const int _minKey, const int _maxKey)
        : numThreads(_numThreads), minKey(_minKey), maxKey(_maxKey) {
    // it may be useful to know about / use the "placement new" operator (google)
    // because the simple_record_manager::allocate does not take constructor arguments
    // ... placement new essentially lets you call a constructor after an object already exists.
    head = new Node();
    tail = new Node();

    initNode(0, head, (casword_t) NULL, (casword_t) tail, _minKey - 1, (casword_t) false);
    initNode(0, tail, (casword_t) head, (casword_t) NULL, _maxKey + 1, (casword_t) false);

}

DoublyLinkedList::~DoublyLinkedList() {
    Node * prev = head;
    Node * curr = (Node *) kcas.readPtr(0, &head->next);

    while(curr != NULL){

        prev = curr;
        curr = (Node *) kcas.readPtr(0, &curr->next);
        delete ((Node *) kcas.readPtr(0, &prev->prev));

    }
    delete prev;
    //delete head;
    //delete tail;
}

void DoublyLinkedList::initNode(const int tid, Node * node, casword_t prev, casword_t next, int val, casword_t mark){
    kcas.writeInitPtr(tid, &node->prev, prev);
    kcas.writeInitPtr(tid, &node->next, next);
    node->val = val;
    kcas.writeInitVal(tid, &node->mark, mark);
}


pair<Node *, Node *> DoublyLinkedList::internalSearch(const int tid, const int & key){
    Node * pred = head;
    Node * succ = head;
    while(true){
        if(succ->val == tail->val || succ->val >= key){
            return make_pair(pred, succ);
        }
        pred = succ;
        succ = (Node *) kcas.readPtr(tid, &succ->next);
    }
}

bool DoublyLinkedList::contains(const int tid, const int & key) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);
    auto [pred, succ] = internalSearch(tid, key);
    return (succ->val == key);
}

bool DoublyLinkedList::insertIfAbsent(const int tid, const int & key) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);

    while(true){
        auto [pred, succ] = internalSearch(tid, key);
        if(succ->val == key){
            return false;
        }
        Node * n = new Node();
        initNode(tid, n, (casword_t) pred, (casword_t) succ, key, (casword_t) false);

        auto descPtr = kcas.getDescriptor(tid);

        descPtr->addValAddr(&pred->mark, (casword_t) false, (casword_t) false);
        descPtr->addValAddr(&succ->mark, (casword_t) false, (casword_t) false);
        
        descPtr->addPtrAddr(&pred->next, (casword_t) succ, (casword_t) n);
        descPtr->addPtrAddr(&succ->prev, (casword_t) pred, (casword_t) n);

        if(kcas.execute(tid, descPtr)){
            return true;
        }
        else{
            delete n;
        }
    }
    assert(false);
}

bool DoublyLinkedList::erase(const int tid, const int & key) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);

    while(true){
        auto [pred, succ] = internalSearch(tid, key);
        if(succ->val != key){
            return false;
        }
        Node * after = (Node *) kcas.readPtr(tid, &succ->next);
        auto descPtr = kcas.getDescriptor(tid);

        descPtr->addValAddr(&pred->mark, (casword_t) false, (casword_t) false);
        descPtr->addValAddr(&succ->mark, (casword_t) false, (casword_t) true);
        descPtr->addValAddr(&after->mark, (casword_t) false, (casword_t) false);

        descPtr->addPtrAddr(&pred->next, (casword_t) succ, (casword_t) after);
        descPtr->addPtrAddr(&after->prev, (casword_t) succ, (casword_t) pred);

        if(kcas.execute(tid, descPtr)){
            return true;
        }

    }
    assert(false);
}

long DoublyLinkedList::getSumOfKeys() {

    Node * temp = (Node *) kcas.readPtr(0, &head->next);
    long total = 0;
    while(temp->val != tail->val){
        total += temp->val;
        temp = (Node *) kcas.readPtr(0, &temp->next);
    }
    return total;
}

void DoublyLinkedList::printDebuggingDetails() {
}
