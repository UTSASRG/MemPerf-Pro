#include "structs.h"
#include "globalstatus.h"

void SizeClassSizeAndIndex::updateValues(size_t size, size_t classSize, unsigned int classSizeIndex) {
    this->size = size;
    this->classSize = classSize;
    this->classSizeIndex = classSizeIndex;
}


void AllocatingType::switchFreeingTypeGotFromMemoryWaste(AllocatingTypeWithSizeGotFromMemoryWaste allocatingTypeWithSizeGotFromMemoryWaste) {
    objectSize = allocatingTypeWithSizeGotFromMemoryWaste.objectSize;
    isALargeObject = ProgramStatus::isALargeObject(objectSize);
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

void SystemCallData::debugPrint() {
    fprintf(stderr, "num = %lu, cycles = %lu\n", num, cycles);
}

void OverviewLockData::add(OverviewLockData newOverviewLockData) {
    numOfLocks += newOverviewLockData.numOfLocks;
    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        numOfCalls[allocationType] += newOverviewLockData.numOfCalls[allocationType];
        numOfCallsWithContentions[allocationType] += newOverviewLockData.numOfCallsWithContentions[allocationType];
        totalCycles[allocationType] += newOverviewLockData.totalCycles[allocationType];
    }
}

void OverviewLockData::debugPrint() {
    fprintf(stderr, "num of locks = %u\n", numOfLocks);
    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        fprintf(stderr, "num of calls = %u, num of calls with contentions = %u, total cycles = %lu\n",
                numOfCalls[allocationType], numOfCallsWithContentions[allocationType], totalCycles[allocationType]);
    }
    fprintf(stderr, "\n");
}


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
    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(GlobalStatus::numOfFunctions[allocationType] && cycles[allocationType]/GlobalStatus::numOfFunctions[allocationType] > 500) {
            return true;
        }
    }
    return false;
}

void DetailLockData::add(DetailLockData newDetailLockData) {
    lockType = newDetailLockData.lockType;
    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        numOfCalls[allocationType] += newDetailLockData.numOfCalls[allocationType];
        numOfCallsWithContentions[allocationType] += newDetailLockData.numOfCallsWithContentions[allocationType];
        cycles[allocationType] += newDetailLockData.cycles[allocationType];
    }
}


void CriticalSectionStatus::add(CriticalSectionStatus newCriticalSectionStatus) {
    numOfCriticalSections += newCriticalSectionStatus.numOfCriticalSections;
    totalCyclesOfCriticalSections += newCriticalSectionStatus.totalCyclesOfCriticalSections;
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
    for(int falseSharingType = 0; falseSharingType < NUM_OF_FALSESHARINGTYPE; ++falseSharingType) {
        numOfSampledFalseSharingInstructions[falseSharingType] += newFriendlinessStatus.numOfSampledFalseSharingInstructions[falseSharingType];
        numOfSampledFalseSharingCacheLines[falseSharingType] += newFriendlinessStatus.numOfSampledFalseSharingCacheLines[falseSharingType];
    }
}

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

bool TotalMemoryUsage::isLowerThan(TotalMemoryUsage newTotalMemoryUsage, size_t interval) {
    return this->totalMemoryUsage + (int64_t)interval < newTotalMemoryUsage.totalMemoryUsage;
}

bool TotalMemoryUsage::isLowerThan(TotalMemoryUsage newTotalMemoryUsage) {
    return this->totalMemoryUsage < newTotalMemoryUsage.totalMemoryUsage;
}

void TotalMemoryUsage::debugPrint() {
    fprintf(stderr, "real memory usage = %ld, total memory usage = %ld\n", realMemoryUsage, totalMemoryUsage);
}


void PerfReadInfo::add(struct PerfReadInfo newPerfReadInfo) {
    faults += newPerfReadInfo.faults;
    tlb_read_misses += newPerfReadInfo.tlb_read_misses;
    tlb_write_misses += newPerfReadInfo.tlb_write_misses;
    cache_misses += newPerfReadInfo.cache_misses;
    instructions += newPerfReadInfo.instructions;
}


void PerfReadInfo::debugPrint() {
    fprintf(stderr, "faults = %ld, tlb_read_misses = %ld, tlb_write_misses = %ld, "
                    "cache_misses = %ld, instructions = %ld\n",
                    faults, tlb_read_misses, tlb_write_misses, cache_misses, instructions);
}
