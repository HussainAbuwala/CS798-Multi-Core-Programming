#pragma once
#include "util.h"
#include <atomic>
#include <mutex>
using namespace std;

class AlgorithmB {
public:
    static constexpr int TOMBSTONE = -1;

    char padding0[PADDING_BYTES];
    const int numThreads;
    int capacity;
    char padding2[PADDING_BYTES];

    BUCKET* data;

    AlgorithmB(const int _numThreads, const int _capacity);
    ~AlgorithmB();
    bool insertIfAbsent(const int tid, const int & key);
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
AlgorithmB::AlgorithmB(const int _numThreads, const int _capacity)
: numThreads(_numThreads), capacity(_capacity) {

    data = new BUCKET[capacity];
    for(int i = 0; i < capacity; ++i){
        data[i].key = 0;
    }
    
}

// destructor: clean up any allocated memory, etc.
AlgorithmB::~AlgorithmB() {
    delete [] data;
}

// semantics: try to insert key. return true if successful (if key doesn't already exist), and false otherwise
bool AlgorithmB::insertIfAbsent(const int tid, const int & key) {

    uint32_t h = murmur3(key);
    for(uint32_t i = 0; i < capacity; ++i){

        uint32_t index = (h + i) % capacity;
        int found = data[index].key;

        if (found == key){
            return false; //LP is last read of data[index].key
        }
        else if (found == 0){
            data[index].m.lock();
            found = data[index].key;
            if (found == 0){
                data[index].key = key; 
                data[index].m.unlock();
                return true; //LP is write to data[index].key
            }
            if (found == key){
                data[index].m.unlock();
                return false; //LP is last read of data[index].key
            }
            data[index].m.unlock();
        }

    }
    return false; //LP is last read of data[index].key
}

// semantics: try to erase key. return true if successful, and false otherwise
bool AlgorithmB::erase(const int tid, const int & key) {

    uint32_t h = murmur3(key);

    for(uint32_t i = 0; i < capacity; ++i){

        uint32_t index = (h + i) % capacity;
        int found = data[index].key;
        if (found == 0){
            return false; //LP is the last read of data[index].key
        }
        else if(found == key){
            data[index].m.lock();
            found = data[index].key;
            if (found == key){
                data[index].key = TOMBSTONE; 
                data[index].m.unlock();
                return true; //LP is write to data[index].key
            }
            if(found == TOMBSTONE){
                data[index].m.unlock();
                return false; //LP is the last read of data[index].key
            }
            data[index].m.unlock();
        }

    }
    return false; //LP is the last read of data[index].key
}

// semantics: return the sum of all KEYS in the set
int64_t AlgorithmB::getSumOfKeys() {
    
    int64_t sum = 0;

    for(int index = 0; index < capacity; ++index){
        data[index].m.lock();
        int found = data[index].key;
        if (found > 0){
            sum += found;
        }
    }

    for(int index = 0; index < capacity; ++index){
        data[index].m.unlock();
    }
    return sum;
}

// print any debugging details you want at the end of a trial in this function
void AlgorithmB::printDebuggingDetails() {
    
}
