#include "threadlocalstatus.h"

unsigned int ThreadLocalStatus::totalNumOfRunningThread;
thread_local int ThreadLocalStatus::runningThreadIndex;
spinlock ThreadLocalStatus::lock;

thread_local uint64_t ThreadLocalStatus::numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local PerfReadInfo ThreadLocalStatus::countingEvents[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local uint64_t ThreadLocalStatus::cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local OverviewLockData ThreadLocalStatus::overviewLockData[NUM_OF_LOCKTYPES];
thread_local CriticalSectionStatus ThreadLocalStatus::criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local SystemCallData ThreadLocalStatus::systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local FriendlinessStatus ThreadLocalStatus::friendlinessStatus;

thread_local bool ThreadLocalStatus::threadIsStopping = false;

void ThreadLocalStatus::getARunningThreadIndex() {
    lock.lock();
    runningThreadIndex = totalNumOfRunningThread++;
    lock.unlock();
}