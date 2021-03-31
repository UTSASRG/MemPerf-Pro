#ifndef MMPROF_THREADLOCALSTATUS_H
#define MMPROF_THREADLOCALSTATUS_H

#include <stdlib.h>
#include <time.h>
#include<random>

#include "memsample.h"
#include "allocatingstatus.h"
#include "definevalues.h"
#include "structs.h"

extern thread_local bool isCountingInit;

class ThreadLocalStatus {
public:
    static unsigned short totalNumOfThread;
    static unsigned short maxNumOfRunningThread;
    static unsigned short totalNumOfRunningThread;
    static thread_local short runningThreadIndex;
    static spinlock lock;

    static thread_local unsigned int numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local unsigned int numOfSampledCountingFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local OverviewLockData overviewLockData[NUM_OF_LOCKTYPES];
    static thread_local CriticalSectionStatus criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local SystemCallData systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local FriendlinessStatus friendlinessStatus;
    static thread_local std::default_random_engine random;
    static thread_local std::uniform_int_distribution<int> dis;

    static void getARunningThreadIndex();
    static void addARunningThread();
    static void subARunningThread();
    static bool isCurrentlySingleThread();
    static bool fromSerialToParallel();
    static bool fromParallelToSerial();

    static void setRandomPeriodForAllocations();
    static bool randomProcessForCountingEvent();
    static bool randomProcessForLargeCountingEvent();
    static bool randomProcess(unsigned short randomPeriod);

};


#endif //MMPROF_THREADLOCALSTATUS_H
