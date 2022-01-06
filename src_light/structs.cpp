#include "structs.h"
#include "globalstatus.h"

void SystemCallData::addOneSystemCall(uint64_t cycles) {
    this->num++;
    this->cycles += cycles;
}

void SystemCallData::add(SystemCallData newSystemCallData) {
    this->num += newSystemCallData.num;
    this->cycles += newSystemCallData.cycles;
}

void OverviewLockData::add(OverviewLockData newOverviewLockData) {
    numOfLocks += newOverviewLockData.numOfLocks;
    for(uint8_t allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        numOfCalls[allocationType] += newOverviewLockData.numOfCalls[allocationType];
        numOfCallsWithContentions[allocationType] += newOverviewLockData.numOfCallsWithContentions[allocationType];
        totalCycles[allocationType] += newOverviewLockData.totalCycles[allocationType];
    }
}

DetailLockData DetailLockData::newDetailLockData(LockTypes lockType) {
    return DetailLockData{lockType, {0}, {0}, 0, 0, 0};
}

bool DetailLockData::isAnImportantLock() {
    for(uint8_t allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(GlobalStatus::numOfSampledCountingFunctions[allocationType] && cycles[allocationType]/GlobalStatus::numOfSampledCountingFunctions[allocationType] > 500) {
            return true;
        }
    }
    return false;
}

void DetailLockData::add(DetailLockData newDetailLockData) {
    lockType = newDetailLockData.lockType;
    for(uint8_t allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        numOfCalls[allocationType] += newDetailLockData.numOfCalls[allocationType];
        numOfCallsWithContentions[allocationType] += newDetailLockData.numOfCallsWithContentions[allocationType];
        cycles[allocationType] += newDetailLockData.cycles[allocationType];
    }
}

#ifdef UTIL

#ifdef CACHE_UTIL
void FriendlinessStatus::recordANewSampling(uint64_t memoryUsageOfCacheLine, uint64_t memoryUsageOfPage) {
    numOfSampling++;
    totalMemoryUsageOfSampledCacheLines += memoryUsageOfCacheLine;
    totalMemoryUsageOfSampledPages += memoryUsageOfPage;
}
#else
void FriendlinessStatus::recordANewSampling(uint64_t memoryUsageOfPage) {
    numOfSampling++;
    totalMemoryUsageOfSampledPages += memoryUsageOfPage;
}
#endif

#else
void FriendlinessStatus::recordANewSampling() {
    numOfSampling++;
}
#endif


void FriendlinessStatus::add(FriendlinessStatus newFriendlinessStatus) {
    numOfSampling += newFriendlinessStatus.numOfSampling;

#ifdef UTIL
    totalMemoryUsageOfSampledPages += newFriendlinessStatus.totalMemoryUsageOfSampledPages;

#ifdef CACHE_UTIL
    totalMemoryUsageOfSampledCacheLines += newFriendlinessStatus.totalMemoryUsageOfSampledCacheLines;
#endif

#endif

//    numOfSampledStoringInstructions += newFriendlinessStatus.numOfSampledStoringInstructions;
//    numOfSampledCacheLines += newFriendlinessStatus.numOfSampledCacheLines;
//    numOfTrueSharing += newFriendlinessStatus.numOfTrueSharing;
//    numOfFalseSharing += newFriendlinessStatus.numOfFalseSharing;
//    cacheConflictDetector.add(newFriendlinessStatus.cacheConflictDetector);
}