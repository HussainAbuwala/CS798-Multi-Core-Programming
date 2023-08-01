#pragma once
#include "util.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <stdlib.h>
using namespace std;


class AlgorithmD {

private:
    enum {
        MARKED_MASK = (int) 0x80000000,     // most significant bit of a 32-bit key
        TOMBSTONE = (int) 0x7FFFFFFF,       // largest value that doesn't use bit MARKED_MASK
        EMPTY = (int) 0
    }; // with these definitions, the largest "real" key we allow in the table is 0x7FFFFFFE, and the smallest is 1 !!

    struct table {
        // data types
        char padding0[PADDING_BYTES];
        ATOMIC_BUCKET * oldData;
        ATOMIC_BUCKET * currData;
        int currCapacity;
        int oldCapacity;
        counter * insertAC;
        counter * deleteAC;
        char padding1[PADDING_BYTES];
        atomic<int> chunksClaimed;
        char padding2[PADDING_BYTES];
        atomic<int> chunksDone;
        char padding3[PADDING_BYTES];

        // constructor
        table(int oldC, ATOMIC_BUCKET * oldD, int currC, int nThreads){

            oldData = oldD;
            oldCapacity = oldC;

            currCapacity = currC;            
            try{
                currData = new ATOMIC_BUCKET[currCapacity];
            }
            catch(std::bad_alloc & ba){
                cout << "BAD ALLOC ****************** CAUGHT" << endl;
                cout << "New Capacity at BAD ALLOC:   " << currCapacity << endl;
                exit(3);
            }
            
            for(int i = 0; i < currCapacity; ++i){
                currData[i].key.store(0, std::memory_order_relaxed);
            }
            
            insertAC = new counter(nThreads);
            deleteAC = new counter(nThreads);
            chunksClaimed = ATOMIC_VAR_INIT(0);
            chunksDone = ATOMIC_VAR_INIT(0);
            
        }

        // destructor

        ~table(){
            delete insertAC;
            delete deleteAC;
            delete[] currData;
        }
        
    };

    //New helper functions added
    int getKey(int key);
    bool isMarked(int key);
    int64_t getSumOld();

    bool expandAsNeeded(const int tid, table * t, int i);
    void helpExpansion(const int tid, table * t);
    void startExpansion(const int tid, table * t);
    void migrate(const int tid, table * t, int myChunk);
    
    char padding0[PADDING_BYTES];
    int numThreads;
    int initCapacity;
    // more fields (pad as appropriate)
    atomic<table *> currentTable;
    char padding1[PADDING_BYTES];
    
public:
    AlgorithmD(const int _numThreads, const int _capacity);
    ~AlgorithmD();
    bool insertIfAbsent(const int tid, const int & key, bool disableExpansion);
    bool erase(const int tid, const int & key);
    long getSumOfKeys();
    void printDebuggingDetails(); 
};

/**
 * constructor: initialize the hash table's internals
 * 
 * @param _numThreads maximum number of threads that will ever use the hash table (i.e., at least tid+1, where tid is the largest thread ID passed to any function of this class)
 * @param _capacity is the INITIAL size of the hash table (maximum number of elements it can contain WITHOUT expansion)
 */
AlgorithmD::AlgorithmD(const int _numThreads, const int _capacity)
: numThreads(_numThreads), initCapacity(_capacity) {
    currentTable = new table(0, NULL, _capacity, _numThreads);
}


// destructor: clean up any allocated memory, etc.
AlgorithmD::~AlgorithmD() {
    //delete currentTable.load()->oldData;
    delete currentTable;
}

int AlgorithmD::getKey(int key){
    return key & (~MARKED_MASK);
}

bool AlgorithmD::isMarked(int key){
    if((key & MARKED_MASK) == MARKED_MASK){
        return true;
    }
    return false;
}

bool AlgorithmD::expandAsNeeded(const int tid, table * t, int i) {
    helpExpansion(tid, t);
    if(((t->insertAC->get()) > t->currCapacity / 2) || (i > 50 && t->insertAC->getAccurate() > t->currCapacity / 2)){
        startExpansion(tid, t);
        return true;
    }
    return false;
}

void AlgorithmD::helpExpansion(const int tid, table * t) {

    int totalOldChunks = ceil((float)t->oldCapacity / 4096);
    while (t->chunksClaimed < totalOldChunks){
        int myChunk = t->chunksClaimed.fetch_add(1);
        if(myChunk < totalOldChunks){
            migrate(tid, t, myChunk);
            t->chunksDone.fetch_add(1);
            //cout << " totalOldChunks: " << totalOldChunks << " chunksClaimed: " << t->chunksClaimed << " chunksDone: " <<  t->chunksDone << endl;
        }
    }
    while (t->chunksDone != totalOldChunks){
        //assert(false);
        //wait till all chunks are migrated
    }

}

void AlgorithmD::startExpansion(const int tid, table * t) {


    if(currentTable.load() == t){
        //cout << " ***************** sum of keys before migration (old struct): " << getSumOfKeys() <<  " ********************** "<<endl;
        //cout << " po_cap: " << t->oldCapacity << " pn_cap: " << t->currCapacity << " chunksClaimed: " << t->chunksClaimed << " chunksDone: " << t->chunksDone << endl;
        int noOfKeys = t->insertAC->getAccurate() - t->deleteAC->getAccurate();
        int newCap = 4 * noOfKeys;
        if(newCap == 0){
            assert(false);
            newCap = 2 * t->currCapacity;
        }
        if (newCap < 0){
            assert(newCap < 0);
        }
        
        
        table * t_new = new table(t->currCapacity, t->currData, newCap, numThreads);
        if(!currentTable.compare_exchange_strong(t, t_new)){
            //delete newly created struct
            delete t_new;            
        }
        else{
            //delete t->insertAC;
            //delete t->deleteAC;
            /*if(t->oldData != NULL){
                delete[] t->oldData;
            }*/
        }
        
        table * ct = currentTable.load();
        //cout << " no_cap: " << ct->oldCapacity << " nn_cap: " << ct->currCapacity << " chunksClaimed: " << ct->chunksClaimed << " chunksDone: " << ct->chunksDone << endl;
    }

    helpExpansion(tid, currentTable.load());

}

void AlgorithmD::migrate(const int tid, table * t, int myChunk) {

    int start_index = myChunk * 4096;
    int end_index = min(start_index + 4096, t->oldCapacity);
    ATOMIC_BUCKET * oldData = t->oldData;
    ATOMIC_BUCKET * currData = t->currData;

    //cout << " chunkID: " << myChunk << " startIndex: " << start_index << " endIndex: " << end_index << endl;
    for(int i = start_index; i < end_index; ++i){
        int found = oldData[i].key;
        while(!oldData[i].key.compare_exchange_strong(found, found | MARKED_MASK)){
            //assert(false);
            found = oldData[i].key;
        }
        int key = getKey(oldData[i].key);
        if(key != TOMBSTONE && key != EMPTY){
            insertIfAbsent(tid, key, true);
        }
    }
    //cout << "-------------- During Migration: Sum oldKeys: " << getSumOld() << " Sum newKeys: " << getSumOfKeys() <<   " -------------- "<<endl;
    //cout << endl;

}

// semantics: try to insert key. return true if successful (if key doesn't already exist), and false otherwise
bool AlgorithmD::insertIfAbsent(const int tid, const int & key, bool disableExpansion = false) {
    
    table * t = currentTable.load();
    ATOMIC_BUCKET * data = t->currData;
    int capacity = t->currCapacity;
    uint32_t h = murmur3(key);

    for(uint32_t i = 0; i < capacity; ++i){

        if(disableExpansion){
            assert(key > 0);
            assert(key != TOMBSTONE);
        }

        if (!disableExpansion && expandAsNeeded(tid, t, i)){
            return insertIfAbsent(tid, key, disableExpansion);
        }

        uint32_t index = (h + i) % capacity;
        int found = data[index].key;
        //int currKey = getKey(found);

        if(!disableExpansion && isMarked(found)){
            return insertIfAbsent(tid, key, disableExpansion);
        }
        else if(found == key){
            if(disableExpansion){
                assert(false);
            }
            return false;
        }
        else if(found == EMPTY){
            if (data[index].key.compare_exchange_strong(found, key)){
                t->insertAC->inc(tid);
                return true;
            }
            else{
                found = data[index].key;
                //currKey = getKey(found);

                if(!disableExpansion && isMarked(found)){
                    return insertIfAbsent(tid, key, disableExpansion); 
                }
                else if (found == key){
                    if(disableExpansion){
                        assert(false);
                    }
                    return false;
                }
            }
        }
    }
    //return false;
    assert(false);
}

// semantics: try to erase key. return true if successful, and false otherwise
bool AlgorithmD::erase(const int tid, const int & key) {

    table * t = currentTable.load();
    ATOMIC_BUCKET * data = t->currData;
    int capacity = t->currCapacity;
    uint32_t h = murmur3(key);

    for(uint32_t i = 0; i < capacity; ++i){
        if(expandAsNeeded(tid, t, i)){
            return erase(tid, key);
        }

        uint32_t index = (h + i) % capacity;
        int found = data[index].key;
        //int currKey = getKey(found);

        if (found == EMPTY){
            return false;
        }
        else if(isMarked(found)){
            return erase(tid, key);
        }
        else if(found == key){
            if (data[index].key.compare_exchange_strong(found, TOMBSTONE)){
                t->deleteAC->inc(tid);
                return true;
            }
            found = data[index].key;
            //currKey = getKey(found);
            if(isMarked(found)){
                return erase(tid, key); 
            }
            else if(found == TOMBSTONE){
                return false;
            }
        }
    }
    return false;
}

// semantics: return the sum of all KEYS in the set
int64_t AlgorithmD::getSumOfKeys() {
    int64_t sum = 0;
    table * t = currentTable.load();
    ATOMIC_BUCKET * data = t->currData;
    int capacity = t->currCapacity;

    for(int index = 0; index < capacity; ++index){
        int found = data[index].key;
        int currKey = getKey(found);
        if (currKey != EMPTY && currKey != TOMBSTONE){
            sum += currKey;
        }
    }
    return sum;
}

int64_t AlgorithmD::getSumOld() {
    int64_t sum = 0;
    table * t = currentTable.load();
    ATOMIC_BUCKET * data = t->oldData;
    int capacity = t->oldCapacity;

    for(int index = 0; index < capacity; ++index){
        int found = data[index].key;
        int currKey = getKey(found);
        if (currKey != EMPTY && currKey != TOMBSTONE){
            sum += currKey;
        }
    }
    return sum;
}

// print any debugging details you want at the end of a trial in this function
void AlgorithmD::printDebuggingDetails() {
    /*
    table * t = currentTable.load();
    ATOMIC_BUCKET * data = t->currData;
    int capacity = t->currCapacity;
    cout << "Final Hash Table Capacity: " << capacity <<endl;
    if(capacity > 50){
        capacity = 50; 
    }

    for(int index = 0; index < capacity; ++index){
        int found = data[index].key;
        int currKey = getKey(found);
        if (currKey == EMPTY){
            cout << " E ";
        }
        else if(currKey == TOMBSTONE){
            cout << " T ";
        }
        else{
            cout << currKey;
        }
    }
    cout <<endl;*/

}
