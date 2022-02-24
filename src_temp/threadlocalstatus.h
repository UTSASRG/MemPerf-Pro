#ifndef MMPROF_THREADLOCALSTATUS_H
#define MMPROF_THREADLOCALSTATUS_H

#include <stdlib.h>
#include <time.h>
#include <random>

#include "memsample.h"
#include "allocatingstatus.h"
#include "definevalues.h"
#include "structs.h"

class ThreadLocalStatus {
public:
    static unsigned short totalNumOfThread;
//    static unsigned short maxNumOfRunningThread;
    static unsigned short totalNumOfRunningThread;
    static thread_local short runningThreadIndex;
    static spinlock lock;

#ifdef COUNTING
    static thread_local PerfReadInfo countingEvents[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
#endif

//    static thread_local unsigned int numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local unsigned int numOfSampledCountingFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local OverviewLockData overviewLockData[NUM_OF_LOCKTYPES];
//    static thread_local CriticalSectionStatus criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local SystemCallData systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local FriendlinessStatus friendlinessStatus;
    static thread_local std::default_random_engine random;
    static thread_local std::uniform_int_distribution<int> dis;

    static thread_local void * stackBottom;

    static void getARunningThreadIndex();
    static void addARunningThread();
    static void subARunningThread();
    static bool isCurrentlySingleThread();
    static bool isCurrentlyParallelThread();

    static bool fromSerialToParallel();
    static bool fromParallelToSerial();

    static void setRandomPeriodForAllocations();
    static bool randomProcessForCountingEvent();
    static bool randomProcessForLargeCountingEvent();
//    static bool randomProcess(unsigned short randomPeriod);
    static void setStackBottom(unsigned int null);
    static uint64_t getStackOffset(void * stackTop);

};


#endif //MMPROF_THREADLOCALSTATUS_H
