#include "structs.h"
#include "globalstatus.h"

void SizeClassSizeAndIndex::updateValues(unsigned int size, unsigned int classSize, unsigned short classSizeIndex) {
    this->size = size;
    this->classSize = classSize;
    this->classSizeIndex = classSizeIndex;
}


void AllocatingType::switchFreeingTypeGotFromMemoryWaste(AllocatingTypeWithSizeGotFromMemoryWaste allocatingTypeWithSizeGotFromMemoryWaste) {
    objectSize = allocatingTypeWithSizeGotFromMemoryWaste.objectSize;
    objectSizeType = ProgramStatus::getObjectSizeType(objectSize);
    allocatingTypeGotFromMemoryWaste = allocatingTypeWithSizeGotFromMemoryWaste.allocatingTypeGotByMemoryWaste;
};

void SystemCallData::addOneSystemCall(uint64_t cycles) {
    this->num++;
    this->cycles += cycles;
}

void SystemCallData::add(SystemCallData newSystemCallData) {
    this->num += newSystemCallData.num;
    this->cycles += newSystemCallData.cycles;
}

void SystemCallData::cleanup() {
    this->num = 0;
    this->cycles = 0;
}

#ifdef OPEN_DEBUG
void SystemCallData::debugPrint() {
    fprintf(stderr, "num = %u, cycles = %lu\n", num, cycles);
}
#endif

void OverviewLockData::add(OverviewLockData newOverviewLockData) {
    numOfLocks += newOverviewLockData.numOfLocks;
    for(uint8_t allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        numOfCalls[allocationType] += newOverviewLockData.numOfCalls[allocationType];
        numOfCallsWithContentions[allocationType] += newOverviewLockData.numOfCallsWithContentions[allocationType];
        totalCycles[allocationType] += newOverviewLockData.totalCycles[allocationType];
    }
}

#ifdef OPEN_DEBUG
void OverviewLockData::debugPrint() {
    fprintf(stderr, "num of locks = %u\n", numOfLocks);
    for(uint8_t allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        fprintf(stderr, "num of calls = %u, num of calls with contentions = %u, total cycles = %lu\n",
                numOfCalls[allocationType], numOfCallsWithContentions[allocationType], totalCycles[allocationType]);
    }
    fprintf(stderr, "\n");
}
#endif

DetailLockData DetailLockData::newDetailLockData(LockTypes lockType) {
    return DetailLockData{lockType, {0}, {0}, 0, 0, 0};
}

bool DetailLockData::aContentionHappening() {
    return (++numOfContendingThreads >= 2);
}

void DetailLockData::checkAndUpdateMaxNumOfContendingThreads() {
    maxNumOfContendingThreads = MAX(numOfContendingThreads, maxNumOfContendingThreads);
}

void DetailLockData::quitFromContending() {
    numOfContendingThreads--;
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


void CriticalSectionStatus::add(CriticalSectionStatus newCriticalSectionStatus) {
    numOfCriticalSections += newCriticalSectionStatus.numOfCriticalSections;
    totalCyclesOfCriticalSections += newCriticalSectionStatus.totalCyclesOfCriticalSections;
}

void CacheConflictDetector::add(CacheConflictDetector newCacheConflictDetector) {
    for(uint8_t cacheIndex = 0; cacheIndex < NUM_CACHELINES_PER_PAGE; ++cacheIndex) {
        numOfHitForCaches[cacheIndex] += newCacheConflictDetector.numOfHitForCaches[cacheIndex];
        numOfDifferentHitForCaches[cacheIndex] += newCacheConflictDetector.numOfDifferentHitForCaches[cacheIndex];
        totalHitIntervalForCaches[cacheIndex] += newCacheConflictDetector.totalHitIntervalForCaches[cacheIndex];
    }
}

void CacheConflictDetector::hit(unsigned long mega_index, uint8_t page_index, uint8_t cache_index, unsigned int time) {
    numOfHitForCaches[cache_index]++;
    if(lastHitMegaIndex[cache_index] != mega_index || lastHitPageIndex[cache_index] != page_index) {
        if(lastHitMegaIndex[cache_index] || lastHitPageIndex[cache_index]) {
            numOfDifferentHitForCaches[cache_index]++;
            totalHitIntervalForCaches[cache_index] += time - lastHitTimeForCaches[cache_index];
        }
        lastHitMegaIndex[cache_index] = mega_index;
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

void FriendlinessStatus::recordANewSampling(uint64_t memoryUsageOfCacheLine, uint64_t memoryUsageOfPage) {
    numOfSampling++;
    totalMemoryUsageOfSampledCacheLines += memoryUsageOfCacheLine;
    totalMemoryUsageOfSampledPages += memoryUsageOfPage;
}

void FriendlinessStatus::add(FriendlinessStatus newFriendlinessStatus) {
    numOfSampling += newFriendlinessStatus.numOfSampling;
    totalMemoryUsageOfSampledPages += newFriendlinessStatus.totalMemoryUsageOfSampledPages;
    totalMemoryUsageOfSampledCacheLines += newFriendlinessStatus.totalMemoryUsageOfSampledCacheLines;
    numOfSampledStoringInstructions += newFriendlinessStatus.numOfSampledStoringInstructions;
    numOfSampledCacheLines += newFriendlinessStatus.numOfSampledCacheLines;
    for(uint8_t falseSharingType = 0; falseSharingType < NUM_OF_FALSESHARINGTYPE; ++falseSharingType) {
        numOfSampledFalseSharingInstructions[falseSharingType] += newFriendlinessStatus.numOfSampledFalseSharingInstructions[falseSharingType];
        numOfSampledFalseSharingCacheLines[falseSharingType] += newFriendlinessStatus.numOfSampledFalseSharingCacheLines[falseSharingType];
    }
    cacheConflictDetector.add(newFriendlinessStatus.cacheConflictDetector);
}

#ifdef OPEN_DEBUG
void FriendlinessStatus::debugPrint() {
    fprintf(stderr, "numOfSampling = %u\n", numOfSampling);
    if(numOfSampling / 100) {
        fprintf(stderr, "totalMemoryUsageOfSampledPages = %lu, avg = %3lu%%\n",
                totalMemoryUsageOfSampledPages,
                totalMemoryUsageOfSampledPages/(numOfSampling/100*PAGESIZE));
        fprintf(stderr, "totalMemoryUsageOfSampledCacheLines = %lu, avg = %3lu%%\n",
                totalMemoryUsageOfSampledCacheLines,
                totalMemoryUsageOfSampledCacheLines/(numOfSampling/100*CACHELINE_SIZE));
        fprintf(stderr, "numOfSampledStoringInstructions = %u\n", numOfSampledStoringInstructions);
        fprintf(stderr, "numOfSampledCacheLines = %u\n", numOfSampledCacheLines);
        fprintf(stderr, "\n");
    }
}
#endif

bool TotalMemoryUsage::isLowerThan(TotalMemoryUsage newTotalMemoryUsage, unsigned int interval) {
    return this->totalMemoryUsage + (int64_t)interval < newTotalMemoryUsage.totalMemoryUsage;
}

bool TotalMemoryUsage::isLowerThan(TotalMemoryUsage newTotalMemoryUsage) {
    return (this->totalMemoryUsage < newTotalMemoryUsage.totalMemoryUsage) ||
           ((this->totalMemoryUsage == newTotalMemoryUsage.totalMemoryUsage) && (this->realMemoryUsage < newTotalMemoryUsage.realMemoryUsage));
}

void TotalMemoryUsage::ifLowerThanReplace(TotalMemoryUsage newTotalMemoryUsage) {
    this->realMemoryUsage = MAX(this->realMemoryUsage, newTotalMemoryUsage.realMemoryUsage);
    this->totalMemoryUsage = MAX(this->totalMemoryUsage, newTotalMemoryUsage.totalMemoryUsage);
}

#ifdef OPEN_DEBUG
void TotalMemoryUsage::debugPrint() {
    fprintf(stderr, "real memory usage = %ld, total memory usage = %ld\n", realMemoryUsage, totalMemoryUsage);
}
#endif

#ifdef OPEN_COUNTING_EVENT
void PerfReadInfo::add(struct PerfReadInfo newPerfReadInfo) {
    faults += newPerfReadInfo.faults;
    cache += newPerfReadInfo.cache;
    instructions += newPerfReadInfo.instructions;
}

#ifdef OPEN_DEBUG
void PerfReadInfo::debugPrint() {
    fprintf(stderr, "faults = %ld, l1cache_load = %lu, l1cache_load_miss = %lu, llc_load = %lu, llc_load_miss = %lu\n",
                    faults, l1cache_load, l1cache_load_miss, llc_load, llc_load_miss);
}
#endif
#endif