#ifndef MMPROF_THREADLOCALSTATUS_H
#define MMPROF_THREADLOCALSTATUS_H

#include <random>
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
#ifdef OPEN_COUNTING_EVENT
    static thread_local PerfReadInfo countingEvents[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
#endif
    static thread_local uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local OverviewLockData overviewLockData[NUM_OF_LOCKTYPES];
    static thread_local CriticalSectionStatus criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local SystemCallData systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local FriendlinessStatus friendlinessStatus;

    static thread_local std::random_device randomDevice;
    static thread_local unsigned short randomPeriodForAllocations;
    static thread_local bool setSampleForCountingEvent;

    static thread_local void * stackStartAddress;

    static void getARunningThreadIndex();
    static void addARunningThread();
    static void subARunningThread();
    static bool isCurrentlySingleThread();
    static bool fromSerialToParallel();
    static bool fromParallelToSerial();

    static void setRandomPeriodForAllocations(unsigned short randomPeriod);
    static bool randomProcessForCountingEvent();
    static bool randomProcess(unsigned short randomPeriod);

    static void setStackStartAddress(void * addr);
    static uint64_t getStackOffset(void * addr);

};


#endif //MMPROF_THREADLOCALSTATUS_H
