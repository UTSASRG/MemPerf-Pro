#include "threadlocalstatus.h"

unsigned short ThreadLocalStatus::totalNumOfThread;
unsigned short ThreadLocalStatus::maxNumOfRunningThread = 0;
unsigned short ThreadLocalStatus::totalNumOfRunningThread;
thread_local short ThreadLocalStatus::runningThreadIndex;
spinlock ThreadLocalStatus::lock;

thread_local unsigned int ThreadLocalStatus::numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {0};
thread_local unsigned int ThreadLocalStatus::numOfSampledCountingFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {0};
thread_local uint64_t ThreadLocalStatus::cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local OverviewLockData ThreadLocalStatus::overviewLockData[NUM_OF_LOCKTYPES];
thread_local CriticalSectionStatus ThreadLocalStatus::criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local SystemCallData ThreadLocalStatus::systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local FriendlinessStatus ThreadLocalStatus::friendlinessStatus;

thread_local unsigned short ThreadLocalStatus::randomPeriodForAllocations;
thread_local bool ThreadLocalStatus::setSampleForCountingEvent;

thread_local void * ThreadLocalStatus::stackStartAddress;


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

void ThreadLocalStatus::setRandomPeriodForAllocations(unsigned short randomPeriod) {
    srand((unsigned)time(NULL));
    randomPeriodForAllocations = randomPeriod;
    setSampleForCountingEvent = true;
}

bool ThreadLocalStatus::randomProcessForCountingEvent() {
    return !setSampleForCountingEvent || rand()%randomPeriodForAllocations == 0;
}

bool ThreadLocalStatus::randomProcess(unsigned short randomPeriod) {
    return rand()%randomPeriod == 0;
}

