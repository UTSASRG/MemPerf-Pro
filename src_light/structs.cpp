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

void CacheConflictDetector::add(CacheConflictDetector newCacheConflictDetector) {
    for(uint8_t cacheIndex = 0; cacheIndex < NUM_CACHELINES_PER_PAGE; ++cacheIndex) {
        numOfHitForCaches[cacheIndex] += newCacheConflictDetector.numOfHitForCaches[cacheIndex];
        numOfDifferentHitForCaches[cacheIndex] += newCacheConflictDetector.numOfDifferentHitForCaches[cacheIndex];
        totalHitIntervalForCaches[cacheIndex] += newCacheConflictDetector.totalHitIntervalForCaches[cacheIndex];
    }
}

void CacheConflictDetector::hit(uint64_t page_index, uint8_t cache_index, unsigned int time) {
    numOfHitForCaches[cache_index]++;
    if(lastHitPageIndex[cache_index] != page_index) {
        if(lastHitMegaIndex[cache_index] || lastHitPageIndex[cache_index]) {
            numOfDifferentHitForCaches[cache_index]++;
            totalHitIntervalForCaches[cache_index] += time - lastHitTimeForCaches[cache_index];
        }
        lastHitPageIndex[cache_index] = page_index;
    }
    lastHitTimeForCaches[cache_index] = time;
}

void CacheConflictDetector::print(unsigned int totalHit) {
    double standardScore = (double)100 / (double)64 / (double)64;
    double maxScore = 0;
    for(uint8_t cacheIndex = 0; cacheIndex < NUM_CACHELINES_PER_PAGE; ++cacheIndex) {
        double RCD = (double)totalHitIntervalForCaches[cacheIndex] / (double)numOfDifferentHitForCaches[cacheIndex];
        double hitRate = (double)numOfHitForCaches[cacheIndex]*100 / (double)totalHit;
        double score = hitRate / RCD;
        double relativeScore = score / standardScore;
//        fprintf(stderr, "score = %lf, standardScore = %lf\n", score, standardScore);
        maxScore = MAX(maxScore, relativeScore);
    }
    fprintf(ProgramStatus::outputFile, "conflict miss score: %lf\n", maxScore);
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
    numOfTrueSharing += newFriendlinessStatus.numOfTrueSharing;
    numOfFalseSharing += newFriendlinessStatus.numOfFalseSharing;
    numThreadSwitch += newFriendlinessStatus.numThreadSwitch;
    cacheConflictDetector.add(newFriendlinessStatus.cacheConflictDetector);
}