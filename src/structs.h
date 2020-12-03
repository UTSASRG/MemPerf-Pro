
#ifndef SRC_STRUCTS_H
#define SRC_STRUCTS_H

#include "definevalues.h"
#include "programstatus.h"

struct SizeClassSizeAndIndex {
    unsigned short classSizeIndex;
    unsigned int size;
    unsigned int classSize;

    void updateValues(unsigned int size, unsigned int classSize, unsigned short classSizeIndex);
};

struct AllocatingTypeGotFromMemoryWaste {
    bool isReusedObject;
    unsigned short objectClassSizeIndex;
    unsigned int objectClassSize;
};

struct AllocatingTypeWithSizeGotFromMemoryWaste {
    unsigned int objectSize;
    AllocatingTypeGotFromMemoryWaste allocatingTypeGotByMemoryWaste;
};

struct AllocatingTypeGotFromShadowMemory {
    unsigned int objectNewTouchedPageSize;
};

struct AllocatingType {
    bool doingAllocation = false;
    ObjectSizeType objectSizeType;
    AllocationFunction allocatingFunction;
    unsigned int objectSize;
    AllocatingTypeGotFromShadowMemory allocatingTypeGotFromShadowMemory;
    void * objectAddress;
    AllocatingTypeGotFromMemoryWaste allocatingTypeGotFromMemoryWaste;

    void switchFreeingTypeGotFromMemoryWaste(AllocatingTypeWithSizeGotFromMemoryWaste allocatingTypeWithSizeGotFromMemoryWaste);
};

struct SystemCallData {
    unsigned int num = 0;
    uint64_t cycles = 0;

    void addOneSystemCall(uint64_t cycles);
    void add(SystemCallData newSystemCallData);
    void cleanup();
#ifdef OPEN_DEBUG
    void debugPrint();
#endif
};

struct OverviewLockData {
    unsigned int numOfLocks;
    unsigned int numOfCalls[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    unsigned int numOfCallsWithContentions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    uint64_t totalCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];

    void add(OverviewLockData newOverviewLockData);
#ifdef OPEN_DEBUG
    void debugPrint();
#endif
};

struct DetailLockData {
    LockTypes     lockType;  // What is the lock type
    unsigned int numOfCalls[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];        // How many invocations
    unsigned int numOfCallsWithContentions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA]; // How many of them have the contention
    unsigned short numOfContendingThreads;    // How many are waiting
    unsigned short maxNumOfContendingThreads; // How many threads are contending on this lock
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
    unsigned int numOfSampledStoringInstructions;
    unsigned int numOfSampledCacheLines;
    unsigned int numOfSampledFalseSharingInstructions[NUM_OF_FALSESHARINGTYPE];
    unsigned int numOfSampledFalseSharingCacheLines[NUM_OF_FALSESHARINGTYPE];
    uint64_t totalMemoryUsageOfSampledPages;
    uint64_t totalMemoryUsageOfSampledCacheLines;

    void recordANewSampling(uint64_t memoryUsageOfCacheLine, uint64_t memoryUsageOfPage);
    void add(FriendlinessStatus newFriendlinessStatus);
#ifdef OPEN_DEBUG
    void debugPrint();
#endif
};

struct TotalMemoryUsage {
    int64_t realMemoryUsage;
    int64_t totalMemoryUsage;
    bool isLowerThan(TotalMemoryUsage newTotalMemoryUsage, unsigned int interval);
    bool isLowerThan(TotalMemoryUsage newTotalMemoryUsage);
    void ifLowerThanReplace(TotalMemoryUsage newTotalMemoryUsage);
#ifdef OPEN_DEBUG
    void debugPrint();
#endif
};

struct PerfReadInfo {
    uint64_t faults = 0;
    uint64_t cache = 0;
    uint64_t instructions = 0;

    void add(struct PerfReadInfo newPerfReadInfo);
#ifdef OPEN_DEBUG
    void debugPrint();
#endif
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
