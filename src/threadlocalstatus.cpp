#include "threadlocalstatus.h"

unsigned int ThreadLocalStatus::totalNumOfThread;
unsigned int ThreadLocalStatus::maxNumOfRunningThread = 0;
unsigned int ThreadLocalStatus::totalNumOfRunningThread;
thread_local int ThreadLocalStatus::runningThreadIndex;
spinlock ThreadLocalStatus::lock;

thread_local uint64_t ThreadLocalStatus::numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local uint64_t ThreadLocalStatus::numOfSampledCountingFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local PerfReadInfo ThreadLocalStatus::countingEvents[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local uint64_t ThreadLocalStatus::cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local OverviewLockData ThreadLocalStatus::overviewLockData[NUM_OF_LOCKTYPES];
thread_local CriticalSectionStatus ThreadLocalStatus::criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local SystemCallData ThreadLocalStatus::systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local FriendlinessStatus ThreadLocalStatus::friendlinessStatus;

thread_local std::random_device ThreadLocalStatus::randomDevice;
thread_local uint64_t ThreadLocalStatus::randomPeriodForCountingEvent;
thread_local bool ThreadLocalStatus::setSampleForCountingEvent;

void ThreadLocalStatus::getARunningThreadIndex() {
    lock.lock();
    runningThreadIndex = totalNumOfThread++;
    if(runningThreadIndex >= MAX_THREAD_NUMBER) {
        fprintf(stderr, "increase MAX_THREAD_NUMBER\n");
        abort();
    }
    lock.unlock();
}

void ThreadLocalStatus::addARunningThread() {
    lock.lock();
    totalNumOfRunningThread++;
    maxNumOfRunningThread = MAX(maxNumOfRunningThread, totalNumOfRunningThread);
    lock.unlock();
}

void ThreadLocalStatus::subARunningThread() {
    lock.lock();
    totalNumOfRunningThread--;
    lock.unlock();
}

bool ThreadLocalStatus::isCurrentlySingleThread() {
    return totalNumOfRunningThread <= 1;
}

bool ThreadLocalStatus::fromSerialToParallel() {
    return totalNumOfRunningThread == 2;
}

bool ThreadLocalStatus::fromParallelToSerial() {
    return totalNumOfRunningThread == 1;
}

void ThreadLocalStatus::setRandomPeriodForCountingEvent(uint64_t randomPeriod) {
    randomPeriodForCountingEvent = randomPeriod;
    setSampleForCountingEvent = true;
}

bool ThreadLocalStatus::randomProcessForCountingEvent() {
//    if(AllocatingStatus::isFirstFunction()) {
//        return false;
//    }
    return !setSampleForCountingEvent || randomDevice()%randomPeriodForCountingEvent == 0;
}

bool ThreadLocalStatus::randomProcess(uint64_t randomPeriod) {
    return randomDevice()%randomPeriod == 0;
}

