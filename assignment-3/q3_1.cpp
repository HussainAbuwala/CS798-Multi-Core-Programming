#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
using namespace std;

bool wants_to_enter[2];
int turn = 0;
int shared_counter = 0;

void threadFunc(int tid, int totalIncrements) {
    
    for (int i = 0; i < totalIncrements; ++i) {
        wants_to_enter[tid] = true;
        while (wants_to_enter[1 - tid]){
            if (turn != tid){
                wants_to_enter[tid] = false;
                while (turn != tid){}
            }
            wants_to_enter[tid] = true;
        }
        //critical section
        shared_counter += 1;
        turn = 1 - tid;
        wants_to_enter[tid] = false;

    }

}

int main(int argc, char ** argv) {
    // read command line args

    int totalIncrements = atoi(argv[1]);

    thread t1(threadFunc, 0, totalIncrements);
    thread t2(threadFunc, 1, totalIncrements);

    t1.join();
    t2.join();

    cout << "Final value of shared counter is: " << shared_counter << endl;
    return 0;

}