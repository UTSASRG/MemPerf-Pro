#include "threadlocalstatus.h"

unsigned short ThreadLocalStatus::totalNumOfThread;
//unsigned short ThreadLocalStatus::maxNumOfRunningThread = 0;
unsigned short ThreadLocalStatus::totalNumOfRunningThread;
thread_local short ThreadLocalStatus::runningThreadIndex;
spinlock ThreadLocalStatus::lock;

//thread_local unsigned int ThreadLocalStatus::numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {0};
thread_local unsigned int ThreadLocalStatus::numOfSampledCountingFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {0};
thread_local uint64_t ThreadLocalStatus::cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local OverviewLockData ThreadLocalStatus::overviewLockData[NUM_OF_LOCKTYPES];
//thread_local CriticalSectionStatus ThreadLocalStatus::criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local SystemCallData ThreadLocalStatus::systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local FriendlinessStatus ThreadLocalStatus::friendlinessStatus;
thread_local std::default_random_engine ThreadLocalStatus::random(time(NULL));
#ifdef OPEN_SAMPLING_FOR_ALLOCS
thread_local std::uniform_int_distribution<int> ThreadLocalStatus::dis(0, RANDOM_PERIOD_FOR_ALLOCS-1);
#else
thread_local std::uniform_int_distribution<int> ThreadLocalStatus::dis(0, 9);
#endif

thread_local void * ThreadLocalStatus::stackBottom;

void ThreadLocalStatus::getARunningThreadIndex() {
    lock.lock();
    runningThreadIndex = totalNumOfThread++;
//    if(runningThreadIndex >= MAX_THREAD_NUMBER) {
//        fprintf(stderr, "increase MAX_THREAD_NUMBER\n");
//        abort();
//    }
    lock.unlock();
}

void ThreadLocalStatus::addARunningThread() {
    lock.lock();
    totalNumOfRunningThread++;
//    maxNumOfRunningThread = MAX(maxNumOfRunningThread, totalNumOfRunningThread);
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

bool ThreadLocalStatus::isCurrentlyParallelThread() {
    return ThreadLocalStatus::totalNumOfRunningThread > 1;
}

bool ThreadLocalStatus::fromSerialToParallel() {
    return totalNumOfRunningThread == 2;
}

bool ThreadLocalStatus::fromParallelToSerial() {
    return totalNumOfRunningThread == 1;
}

void ThreadLocalStatus::setRandomPeriodForAllocations() {
    srand((unsigned)time(NULL));
}

bool ThreadLocalStatus::randomProcessForCountingEvent() {
//    return false;
    return dis(random) == 0;
//    return !AllocatingStatus::numFunc;
}

void ThreadLocalStatus::setStackBottom(unsigned int null) {
    stackBottom = &null;
}

uint64_t ThreadLocalStatus::getStackOffset(void * stackTop) {
    return (uint64_t)stackBottom - (uint64_t)stackTop;
}
