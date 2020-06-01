
#ifndef SRC_STRUCTS_H
#define SRC_STRUCTS_H

#include "definevalues.h"
#include "programstatus.h"

struct SizeClassSizeAndIndex {
    size_t size;
    size_t classSize;
    unsigned int classSizeIndex;

    void updateValues(size_t size, size_t classSize, unsigned int classSizeIndex);
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

    void switchFreeingTypeGotFromMemoryWaste(AllocatingTypeWithSizeGotFromMemoryWaste allocatingTypeWithSizeGotFromMemoryWaste);
};

struct SystemCallData {
    uint64_t num;
    uint64_t cycles;

    void add(SystemCallData newSystemCallData);
};

typedef int (*LockFunction) (void *);
typedef struct {
    int (*LockFunction)(void *);
} LockFunctionType;
extern LockFunctionType lockFunctions[NUM_OF_LOCKTYPES];

typedef int (*UnlockFunction) (void *);
typedef struct {
    int (*UnlockFunction)(void *);
} UnlockFunctionType;
extern UnlockFunctionType unlockFunctions[NUM_OF_LOCKTYPES];

struct OverviewLockData {
    unsigned int numOfLocks;
    unsigned int numOfCalls[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    unsigned int numOfCallsWithContentions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    uint64_t totalCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];

    void add(OverviewLockData newOverviewLockData);
};

struct DetailLockData {
    LockTypes     lockType;  // What is the lock type
    unsigned int numOfCalls[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];        // How many invocations
    unsigned int numOfCallsWithContentions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA]; // How many of them have the contention
    unsigned int numOfContendingThreads;    // How many are waiting
    unsigned int maxNumOfContendingThreads; // How many threads are contending on this lock
    uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA]; // Total cycles

    static DetailLockData newDetailLockData(LockTypes lockType);
    bool aContentionHappening();
    void checkAndUpdateMaxNumOfContendingThreads();
    void quitFromContending();
    bool isAnImportantLock();
};

struct CriticalSectionStatus {
    unsigned int numOfCriticalSections;
    uint64_t totalCyclesOfCriticalSections;

    void add(CriticalSectionStatus newCriticalSectionStatus);
};

struct FriendlinessStatus {
    unsigned int numOfSampling;
    uint64_t totalMemoryUsageOfSampledPages;
    uint64_t totalMemoryUsageOfSampledCacheLines;
    unsigned int numOfSampledStoringInstructions;
    unsigned int numOfSampledCacheLines;
    unsigned int numOfSampledFalseSharingInstructions[NUM_OF_FALSESHARINGTYPE];
    unsigned int numOfSampledFalseSharingCacheLines[NUM_OF_FALSESHARINGTYPE];

    void recordANewSampling(uint64_t memoryUsageOfCacheLine, uint64_t memoryUsageOfPage);
    void add(FriendlinessStatus newFriendlinessStatus);
};

struct TotalMemoryUsage {
    uint64_t realMemoryUsage;
    uint64_t totalMemoryUsage;
    bool isLowerThan(TotalMemoryUsage newTotalMemoryUsage, size_t interval);
    bool isLowerThan(TotalMemoryUsage newTotalMemoryUsage);
};

struct PerfReadInfo {
    uint64_t faults = 0;
    uint64_t tlb_read_misses = 0;
    uint64_t tlb_write_misses = 0;
    uint64_t cache_misses = 0;
    uint64_t instructions = 0;

    void add(struct PerfReadInfo newPerfReadInfo);
};

#endif //SRC_STRUCTS_H
