#ifndef COUNTERS_IMPL_H
#define COUNTERS_IMPL_H

#include <atomic>
#include <mutex>

#define MAX_THREADS 256

class CounterNaive {
private:
    char padding0[64];
    int v;
    char padding1[64];
public:
    CounterNaive(int _numThreads) : v(0) {}
    int64_t inc(int tid) {
        return v++;
    }
    int64_t read() {
        return v;
    }
};

class CounterLocked {
private:
    std::mutex m;
    int v;
public:
    CounterLocked(int _numThreads) {
        v = 0;
    }
    int64_t inc(int tid) {
        m.lock();
        auto ret = v++;
        m.unlock();
        return ret;

    }
    int64_t read() {
        m.lock();
        auto ret = v;
        m.unlock();
        return ret;
    }
};

class CounterFetchAndAdd {
private:
    atomic<int> v;
public:
    CounterFetchAndAdd(int _numThreads) {
    }
    int64_t inc(int tid) {
        return v++;
    }
    int64_t read() {
        return v;
    }
};


class CounterApproximate {
private:
    const int c = 1000;
    int numThreads;
    // this padding is important to prevent false sharing.
    char prevP[60];
    struct padded_vint {
        int64_t localCount;
        char nextP[192-sizeof(int64_t)];
    };
    padded_vint localCounts[MAX_THREADS];
    atomic<int64_t> globalCount = ATOMIC_VAR_INIT(0);

public:
    CounterApproximate(int _numThreads) {
        for (int i = 0; i < MAX_THREADS; ++i){
            localCounts[i].localCount = 0;
        }
        numThreads = _numThreads;
    }
    int64_t inc(int tid) {
        
        int64_t current_val = localCounts[tid].localCount;
        if(current_val >= (c * numThreads)){
            globalCount.fetch_add(current_val);
            localCounts[tid].localCount = 0;
        }
        localCounts[tid].localCount += 1;
        return current_val;
    }
    int64_t read() {
        return globalCount;
    }
};


class CounterShardedLocked {
private:
    int numThreads;
    struct LCS {
        int64_t count = 0;
        std::mutex mutex;
        char padding[192-sizeof(int64_t)-sizeof(std::mutex)];
    };
    LCS shards[MAX_THREADS];

public:
    CounterShardedLocked(int _numThreads) {
        for (int i = 0; i < MAX_THREADS; ++i){
            shards[i].count = 0;
        }
        numThreads = _numThreads;
    }
    int64_t inc(int tid) {
        shards[tid].mutex.lock();
        shards[tid].count++;
        shards[tid].mutex.unlock();
        return 0;
    }
    int64_t read() {
        // First all the locks are acquired and are released one by one only after all the local counters are added. This
        // makes the proof easier.
        int64_t sum = 0;
        for(int tid=0; tid <numThreads; ++tid){
            shards[tid].mutex.lock();
            sum += shards[tid].count;
        }
        for(int tid=0; tid <numThreads; ++tid){
            shards[tid].mutex.unlock();
        }
        return sum;
    }
};


class CounterShardedWaitfree {
private:
    int numThreads;
    struct WFCS {
        atomic<int64_t> count;
        char padding[192-sizeof(int64_t)];
    };
    WFCS shards[MAX_THREADS];
public:
    CounterShardedWaitfree(int _numThreads) {
        for(int i = 0; i < MAX_THREADS; ++i){
           shards[i].count = ATOMIC_VAR_INIT(0);
        }          
        numThreads = _numThreads;
    }
    int64_t inc(int tid) {
        shards[tid].count++;
    }
    int64_t read() {
        // sum is made atmoic to make sure the read and write is indivisible, which makes it easier to prove.
        atomic<int64_t> sum = ATOMIC_VAR_INIT(0);
        for(int tid=0; tid <numThreads; ++tid){
            sum += shards[tid].count;
        }
        return sum;
    }
};

#endif

