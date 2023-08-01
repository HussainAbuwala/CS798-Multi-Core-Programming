#pragma once

#include <cassert>
#include "recordmgr/record_manager.h"
#include "kcas.h"

class DoublyLinkedListReclaim {
private:
    volatile char padding0[PADDING_BYTES];
    const int numThreads;
    const int minKey;
    const int maxKey;
    volatile char padding1[PADDING_BYTES];

    Node * head;
    Node * tail;
    KCASLockFree<5> kcas;
    simple_record_manager<Node> * recmgr;

public:
    DoublyLinkedListReclaim(const int _numThreads, const int _minKey, const int _maxKey);
    ~DoublyLinkedListReclaim();
    void initNode(const int tid, Node * node, casword_t prev, casword_t next, int val, casword_t mark);
    pair<Node *, Node *> internalSearch(const int tid, const int & key);    
    bool contains(const int tid, const int & key);
    bool insertIfAbsent(const int tid, const int & key); // try to insert key; return true if successful (if it doesn't already exist), false otherwise
    bool erase(const int tid, const int & key); // try to erase key; return true if successful, false otherwise
    
    long getSumOfKeys(); // should return the sum of all keys in the set
    void printDebuggingDetails(); // print any debugging details you want at the end of a trial in this function
};

DoublyLinkedListReclaim::DoublyLinkedListReclaim(const int _numThreads, const int _minKey, const int _maxKey)
        : numThreads(_numThreads), minKey(_minKey), maxKey(_maxKey) {
    // it may be useful to know about / use the "placement new" operator (google)
    // because the simple_record_manager::allocate does not take constructor arguments
    // ... placement new essentially lets you call a constructor after an object already exists.
    recmgr = new simple_record_manager<Node>(MAX_THREADS);
    auto guard = recmgr->getGuard(0);
    
    head = recmgr->allocate<Node>(0);
    tail = recmgr->allocate<Node>(0);
    
    initNode(0, head, (casword_t) NULL, (casword_t) tail, _minKey - 1, (casword_t) false);
    initNode(0, tail, (casword_t) head, (casword_t) NULL, _maxKey + 1, (casword_t) false);

    
}

DoublyLinkedListReclaim::~DoublyLinkedListReclaim() {

    Node * prev = head;
    Node * curr = (Node *) kcas.readPtr(0, &head->next);

    while(curr != NULL){

        prev = curr;
        curr = (Node *) kcas.readPtr(0, &curr->next);
        recmgr->deallocate(0, (Node *) kcas.readPtr(0, &prev->prev));

    }
    recmgr->deallocate(0, prev); 
    delete recmgr;
}

void DoublyLinkedListReclaim::initNode(const int tid, Node * node, casword_t prev, casword_t next, int val, casword_t mark){
    kcas.writeInitPtr(tid, &node->prev, prev);
    kcas.writeInitPtr(tid, &node->next, next);
    node->val = val;
    kcas.writeInitVal(tid, &node->mark, mark);
}

pair<Node *, Node *> DoublyLinkedListReclaim::internalSearch(const int tid, const int & key){
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

bool DoublyLinkedListReclaim::contains(const int tid, const int & key) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);
    auto guard = recmgr->getGuard(tid);
    auto [pred, succ] = internalSearch(tid, key);
    return (succ->val == key);
}

bool DoublyLinkedListReclaim::insertIfAbsent(const int tid, const int & key) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);
    auto guard = recmgr->getGuard(tid);
    while(true){
        auto [pred, succ] = internalSearch(tid, key);
        if(succ->val == key){
            return false;
        }
        Node * n = recmgr->allocate<Node>(tid);
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
            recmgr->deallocate(tid, n);
        }
    }
    assert(false);
}

bool DoublyLinkedListReclaim::erase(const int tid, const int & key) {
    assert(key > minKey - 1 && key >= minKey && key <= maxKey && key < maxKey + 1);
    auto guard = recmgr->getGuard(tid);

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
            recmgr->retire(tid, succ);
            return true;
        }

    }
    assert(false);
}

long DoublyLinkedListReclaim::getSumOfKeys() {
    auto guard = recmgr->getGuard(0);
    Node * temp = (Node *) kcas.readPtr(0, &head->next);
    long total = 0;
    while(temp->val != tail->val){
        total += temp->val;
        temp = (Node *) kcas.readPtr(0, &temp->next);
    }
    return total;
}

void DoublyLinkedListReclaim::printDebuggingDetails() {
    
}
