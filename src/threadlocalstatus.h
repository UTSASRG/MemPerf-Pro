#ifndef MMPROF_THREADLOCALSTATUS_H
#define MMPROF_THREADLOCALSTATUS_H

#include <random>
#include "memsample.h"
#include "allocatingstatus.h"
#include "definevalues.h"
#include "structs.h"

class ThreadLocalStatus {
public:
    static unsigned int totalNumOfThread;
    static unsigned int totalNumOfRunningThread;
    static thread_local int runningThreadIndex;
    static spinlock lock;

    static thread_local uint64_t numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local uint64_t numOfSampledCountingFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local PerfReadInfo countingEvents[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local OverviewLockData overviewLockData[NUM_OF_LOCKTYPES];
    static thread_local CriticalSectionStatus criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local SystemCallData systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local FriendlinessStatus friendlinessStatus;

    static thread_local bool threadIsStopping;

    static thread_local std::random_device randomDevice;
    static thread_local uint64_t randomPeriodForCountingEvent;
    static thread_local bool setSampleForCountingEvent;

    static void getARunningThreadIndex();
    static void addARunningThread();
    static void subARunningThread();
    static bool isCurrentlySingleThread();

    static void setRandomPeriodForCountingEvent(uint64_t randomPeriod);
    static bool randomProcessForCountingEvent();
    static bool randomProcess(uint64_t randomPeriod);

};


#endif //MMPROF_THREADLOCALSTATUS_H
