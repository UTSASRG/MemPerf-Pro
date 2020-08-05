
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
    uint64_t objectClassSizeIndex;
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
    uint64_t numOfSampling;
    uint64_t totalMemoryUsageOfSampledPages;
    uint64_t totalMemoryUsageOfSampledCacheLines;
    uint64_t numOfSampledStoringInstructions;
    uint64_t numOfSampledCacheLines;
    uint64_t numOfSampledFalseSharingInstructions[NUM_OF_FALSESHARINGTYPE];
    uint64_t numOfSampledFalseSharingCacheLines[NUM_OF_FALSESHARINGTYPE];

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
        (char*)"serial small new malloc      ",
        (char*)"serial medium new malloc     ",
        (char*)"serial small reused malloc   ",
        (char*)"serial medium reused malloc  ",
        (char*)"serial large malloc          ",

        (char*)"serial small free            ",
        (char*)"serial medium free           ",
        (char*)"serial large free            ",

        (char*)"serial calloc                ",
        (char*)"serial realloc               ",
        (char*)"serial posix_memalign        ",
        (char*)"serial memalign              ",

        (char*)"parallel small new malloc    ",
        (char*)"parallel medium new malloc   ",
        (char*)"parallel small reused malloc ",
        (char*)"parallel medium reused malloc",
        (char*)"parallel large malloc        ",

        (char*)"parallel small free          ",
        (char*)"parallel medium free         ",
        (char*)"parallel large free          ",

        (char*)"parallel calloc              ",
        (char*)"parallel realloc             ",
        (char*)"parallel posix_memalign      ",
        (char*)"parallel memalign            "
};

constexpr char * allocationTypeOutputTitleString[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {
        (char*)"SERIAL SMALL NEW MALLOC",
        (char*)"SERIAL MEDIUM NEW MALLOC",
        (char*)"SERIAL SMALL REUSED MALLOC",
        (char*)"SERIAL MEDIUM REUSED MALLOC",
        (char*)"SERIAL LARGE MALLOC",

        (char*)"SERIAL SMALL FREE",
        (char*)"SERIAL MEDIUM FREE",
        (char*)"SERIAL LARGE FREE",

        (char*)"SERIAL CALLOC",
        (char*)"SERIAL REALLOC",
        (char*)"SERIAL POSIX_MEMALIGN",
        (char*)"SERIAL MEMALIGN",

        (char*)"PARALLEL SMALL NEW MALLOC",
        (char*)"PARALLEL MEDIUM NEW MALLOC",
        (char*)"PARALLEL SMALL REUSED MALLOC",
        (char*)"PARALLEL MEDIUM REUSED MALLOC",
        (char*)"PARALLEL LARGE MALLOC",

        (char*)"PARALLEL SMALL FREE",
        (char*)"PARALLEL MEDIUM FREE",
        (char*)"PARALLEL LARGE FREE",

        (char*)"PARALLEL CALLOC",
        (char*)"PARALLEL REALLOC",
        (char*)"PARALLEL POSIX_MEMALIGN",
        (char*)"PARALLEL MEMALIGN",
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
