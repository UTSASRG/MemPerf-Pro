//
// Created by 86152 on 2020/5/23.
//

#ifndef MMPROF_THREADLOCALSTATUS_H
#define MMPROF_THREADLOCALSTATUS_H

class ThreadLocalStatus {
private:
public:
    static thread_local unsigned int runningThreadIndex;

    static thread_local uint64_t numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local PerfReadInfo countingEvents[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];

};

#endif //MMPROF_THREADLOCALSTATUS_H
