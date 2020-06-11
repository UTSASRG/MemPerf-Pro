#include "structs.h"

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
    return maxNumOfContendingThreads >= 10;
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
    totalMemoryUsageOfSampledCacheLines += newFriendlinessStatus.totalMemoryUsageOfSampledPages;
    numOfSampledStoringInstructions += newFriendlinessStatus.numOfSampledStoringInstructions;
    numOfSampledCacheLines += newFriendlinessStatus.numOfSampledCacheLines;
    for(int falseSharingType = 0; falseSharingType < NUM_OF_FALSESHARINGTYPE; ++falseSharingType) {
        numOfSampledFalseSharingInstructions[falseSharingType] += newFriendlinessStatus.numOfSampledFalseSharingInstructions[falseSharingType];
        numOfSampledFalseSharingCacheLines[falseSharingType] += newFriendlinessStatus.numOfSampledFalseSharingCacheLines[falseSharingType];
    }
}

void FriendlinessStatus::debugPrint() {
    fprintf(stderr, "numOfSampling = %u\n", numOfSampling);
    fprintf(stderr, "totalMemoryUsageOfSampledPages = %lu\n", totalMemoryUsageOfSampledPages);
    fprintf(stderr, "totalMemoryUsageOfSampledCacheLines = %lu\n", totalMemoryUsageOfSampledCacheLines);
    fprintf(stderr, "numOfSampledStoringInstructions = %u\n", numOfSampledStoringInstructions);
    fprintf(stderr, "numOfSampledCacheLines = %u\n", numOfSampledCacheLines);
    fprintf(stderr, "\n");
}

bool TotalMemoryUsage::isLowerThan(TotalMemoryUsage newTotalMemoryUsage, size_t interval) {
    return this->totalMemoryUsage + interval < newTotalMemoryUsage.totalMemoryUsage;
}

bool TotalMemoryUsage::isLowerThan(TotalMemoryUsage newTotalMemoryUsage) {
    return this->totalMemoryUsage < newTotalMemoryUsage.totalMemoryUsage;
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
