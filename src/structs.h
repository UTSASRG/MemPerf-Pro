
#ifndef SRC_STRUCTS_H
#define SRC_STRUCTS_H

#include "definevalues.h"
#include "programstatus.h"


struct SizeClassSizeAndIndex {
    size_t size;
    size_t classSize;
    unsigned int classSizeIndex;
    void updateValues(size_t size, size_t classSize, unsigned int classSizeIndex) {
        this->size = size;
        this->classSize = classSize;
        this->classSizeIndex = classSizeIndex;
    }
};

struct AllocatingTypeGotFromMemoryWaste {
    bool isReusedObject;
    size_t objectClassSize;
};
struct AllocatingTypeWithSizeGotFromMemoryWaste {
    size_t objectSize;
    AllocatingTypeGotFromMemoryWaste allocatingTypeGotByMemoryWaste;
};
struct AllocatingTypeGotFromShadowMemory {
    size_t objectNewTouchedPageSize;
};
struct AllocatingType {
    AllocationFunction allocatingFunction;
    size_t objectSize;
    bool isALargeObject;
    bool doingAllocation = false;
    void * objectAddress;
    AllocatingTypeGotFromMemoryWaste allocatingTypeGotFromMemoryWaste;
    AllocatingTypeGotFromShadowMemory allocatingTypeGotFromShadowMemory;

    void switchFreeingTypeGotFromMemoryWaste(AllocatingTypeWithSizeGotFromMemoryWaste allocatingTypeWithSizeGotFromMemoryWaste) {
        objectSize = allocatingTypeWithSizeGotFromMemoryWaste.objectSize;
        isALargeObject = ProgramStatus::isALargeObject(objectSize);
        allocatingTypeGotFromMemoryWaste = allocatingTypeWithSizeGotFromMemoryWaste.allocatingTypeGotByMemoryWaste;
    };
};

struct SystemCallData {
    uint64_t num;
    uint64_t cycles;

    void add(SystemCallData newSystemCallData) {
        this->num += newSystemCallData.num;
        this->cycles += newSystemCallData.cycles;
    }
};

struct OverviewLockData {
    unsigned int numOfLocks;
    unsigned int numOfCalls[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    unsigned int numOfCallsWithContentions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    uint64_t totalCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];

    void add(OverviewLockData newOverviewLockData) {
        numOfLocks += newOverviewLockData.numOfLocks;
        for(int allocationType; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
            numOfCalls[allocationType] += newOverviewLockData.numOfCalls[allocationType];
            numOfCallsWithContentions[allocationType] += newOverviewLockData.numOfCallsWithContentions[allocationType];
            totalCycles[allocationType] += newOverviewLockData.totalCycles[allocationType];
        }
    }
};

struct DetailLockData {
    LockTypes     lockType;  // What is the lock type
    unsigned int numOfCalls[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];        // How many invocations
    unsigned int numOfCallsWithContentions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA]; // How many of them have the contention
    unsigned int numOfContendingThreads;    // How many are waiting
    unsigned int maxNumOfContendingThreads; // How many threads are contending on this lock
    uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA]; // Total cycles

    static DetailLockData newDetailLockData(LockTypes lockType) {
        return DetailLockData{lockType, {0}, {0}, 0, 0, 0};
    }

    bool aContentionHappening() {
        return (++numOfContendingThreads >= 2);
    }

    void checkAndUpdateMaxNumOfContendingThreads() {
        maxNumOfContendingThreads = MAX(numOfContendingThreads, maxNumOfContendingThreads);
    }

    void quitFromContending() {
        numOfContendingThreads--;
    }

    bool isAnImportantLock() {
        return maxNumOfContendingThreads >= 10;
    }
};

struct CriticalSectionStatus {
    unsigned int numOfCriticalSections;
    uint64_t totalCyclesOfCriticalSections;

    void add(CriticalSectionStatus newCriticalSectionStatus) {
        numOfCriticalSections += newCriticalSectionStatus.numOfCriticalSections;
        totalCyclesOfCriticalSections += newCriticalSectionStatus.totalCyclesOfCriticalSections;
    }
};

struct FriendlinessStatus {
    unsigned int numOfSampling;
    uint64_t totalMemoryUsageOfSampledPages;
    uint64_t totalMemoryUsageOfSampledCacheLines;
    unsigned int numOfSampledStoringInstructions;
    unsigned int numOfSampledCacheLines;
    unsigned int numOfSampledFalseSharingInstructions[NUM_OF_FALSESHARINGTYPE];
    unsigned int numOfSampledFalseSharingCacheLines[NUM_OF_FALSESHARINGTYPE];

    void recordANewSampling(uint64_t memoryUsageOfCacheLine, uint64_t memoryUsageOfPage) {
        numOfSampling++;
        totalMemoryUsageOfSampledCacheLines += memoryUsageOfCacheLine;
        totalMemoryUsageOfSampledPages += memoryUsageOfPage;
    }

    void add(FriendlinessStatus newFriendlinessStatus) {
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
};

struct PerfReadInfo {
    uint64_t faults = 0;
    uint64_t tlb_read_misses = 0;
    uint64_t tlb_write_misses = 0;
    uint64_t cache_misses = 0;
    uint64_t instructions = 0;

    void add(struct PerfReadInfo newPerfReadInfo) {
        faults += newPerfReadInfo.faults;
        tlb_read_misses += newPerfReadInfo.tlb_read_misses;
        tlb_write_misses += newPerfReadInfo.tlb_write_misses;
        cache_misses += newPerfReadInfo.cache_misses;
        instructions += newPerfReadInfo.instructions;
    }
};

#endif //SRC_STRUCTS_H
