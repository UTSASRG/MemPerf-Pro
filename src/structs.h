
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
    ObjectSizeType objectSizeType;
    bool doingAllocation = false;
    void * objectAddress;
    AllocatingTypeGotFromMemoryWaste allocatingTypeGotFromMemoryWaste;
    AllocatingTypeGotFromShadowMemory allocatingTypeGotFromShadowMemory;

    void switchFreeingTypeGotFromMemoryWaste(AllocatingTypeWithSizeGotFromMemoryWaste allocatingTypeWithSizeGotFromMemoryWaste);
};

struct SystemCallData {
    uint64_t num = 0;
    uint64_t cycles = 0;

    void addOneSystemCall(uint64_t cycles);
    void add(SystemCallData newSystemCallData);
    void cleanup();
    void debugPrint();
};

struct OverviewLockData {
    unsigned int numOfLocks;
    unsigned int numOfCalls[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    unsigned int numOfCallsWithContentions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    uint64_t totalCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];

    void add(OverviewLockData newOverviewLockData);
    void debugPrint();
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
    void add(DetailLockData newDetailLockData);
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
    void debugPrint();
};

struct TotalMemoryUsage {
    int64_t realMemoryUsage;
    int64_t totalMemoryUsage;
    bool isLowerThan(TotalMemoryUsage newTotalMemoryUsage, size_t interval);
    bool isLowerThan(TotalMemoryUsage newTotalMemoryUsage);
    void ifLowerThanReplace(TotalMemoryUsage newTotalMemoryUsage);
    void debugPrint();
};

struct PerfReadInfo {
    uint64_t faults = 0;
    uint64_t tlb_read_misses = 0;
    uint64_t tlb_write_misses = 0;
    uint64_t cache_misses = 0;
    uint64_t instructions = 0;

    void add(struct PerfReadInfo newPerfReadInfo);
    void debugPrint();
};

constexpr char * outputTitleNotificationString[2] = {
        (char*)"\n>>>>>>>>>>>>>>>",
        (char*)"<<<<<<<<<<<<<<<\n"
};

constexpr char * allocationTypeOutputString[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {
        (char*)"single thread small new malloc    ",
        (char*)"single thread medium new malloc   ",
        (char*)"single thread small reused malloc ",
        (char*)"single thread medium reused malloc",
        (char*)"single thread large malloc        ",

        (char*)"single thread small free          ",
        (char*)"single thread medium free         ",
        (char*)"single thread large free          ",

        (char*)"single thread calloc              ",
        (char*)"single thread realloc             ",
        (char*)"single thread posix_memalign      ",
        (char*)"single thread memalign            ",

        (char*)"multi  thread small new malloc    ",
        (char*)"multi  thread medium new malloc   ",
        (char*)"multi  thread small reused malloc ",
        (char*)"multi  thread medium reused malloc",
        (char*)"multi  thread large malloc        ",

        (char*)"multi  thread small free          ",
        (char*)"multi  thread medium free         ",
        (char*)"multi  thread large free          ",

        (char*)"multi  thread calloc              ",
        (char*)"multi  thread realloc             ",
        (char*)"multi  thread posix_memalign      ",
        (char*)"multi  thread memalign            "
};

constexpr char * allocationTypeOutputTitleString[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {
        (char*)"SINGLE THREAD SMALL NEW MALLOC",
        (char*)"SINGLE THREAD MEDIUM NEW MALLOC",
        (char*)"SINGLE THREAD SMALL REUSED MALLOC",
        (char*)"SINGLE THREAD MEDIUM REUSED MALLOC",
        (char*)"SINGLE THREAD LARGE MALLOC",

        (char*)"SINGLE THREAD SMALL FREE",
        (char*)"SINGLE THREAD MEDIUM FREE",
        (char*)"SINGLE THREAD LARGE FREE",

        (char*)"SINGLE THREAD CALLOC",
        (char*)"SINGLE THREAD REALLOC",
        (char*)"SINGLE THREAD POSIX_MEMALIGN",
        (char*)"SINGLE THREAD MEMALIGN",

        (char*)"MULTI THREAD SMALL NEW MALLOC",
        (char*)"MULTI THREAD MEDIUM NEW MALLOC",
        (char*)"MULTI THREAD SMALL REUSED MALLOC",
        (char*)"MULTI THREAD MEDIUM REUSED MALLOC",
        (char*)"MULTI THREAD LARGE MALLOC",

        (char*)"MULTI THREAD SMALL FREE",
        (char*)"MULTI THREAD MEDIUM FREE",
        (char*)"MULTI THREAD LARGE FREE",

        (char*)"MULTI THREAD CALLOC",
        (char*)"MULTI THREAD REALLOC",
        (char*)"MULTI THREAD POSIX_MEMALIGN",
        (char*)"MULTI THREAD MEMALIGN",
};

constexpr char * lockTypeOutputString[NUM_OF_LOCKTYPES] = {
        (char*)"mutex lock    ",
        (char*)"spin lock     ",
        (char*)"mutex try lock",
        (char*)"spin try lock "
};

constexpr char * syscallTypeOutputString[NUM_OF_SYSTEMCALLTYPES] = {
        (char*)"mmap    ",
        (char*)"madvise ",
        (char*)"sbrk    ",
        (char*)"mprotect",
        (char*)"munmap  ",
        (char*)"mremap  "
};

constexpr char * falseSharingTypeOutputString[NUM_OF_FALSESHARINGTYPE] = {
        (char*)"object false sharing ",
        (char*)"active false sharing ",
        (char*)"passive false sharing"
};

#endif //SRC_STRUCTS_H
