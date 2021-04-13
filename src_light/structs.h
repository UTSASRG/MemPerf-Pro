
#ifndef SRC_STRUCTS_H
#define SRC_STRUCTS_H

#include "definevalues.h"
#include "programstatus.h"


struct AllocatingTypeWithSizeGotFromObjTable {
    bool isReuse;
    unsigned int objectSize;
};

struct AllocatingType {
    bool doingAllocation = false;
    ObjectSizeType objectSizeType;

#ifdef PREDICTION
    ObjectSizeType objectSizeTypeForPrediction;
#endif

    AllocationFunction allocatingFunction;
    unsigned int objectSize;
    void * objectAddress;
    bool isReuse;
};

struct SystemCallData {
    unsigned int num = 0;
    uint64_t cycles = 0;

    void addOneSystemCall(uint64_t cycles);
    void add(SystemCallData newSystemCallData);
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
//    unsigned short numOfContendingThreads;    // How many are waiting
//    unsigned short maxNumOfContendingThreads; // How many threads are contending on this lock
    uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA]; // Total cycles

    static DetailLockData newDetailLockData(LockTypes lockType);
//    bool aContentionHappening();
//    void checkAndUpdateMaxNumOfContendingThreads();
//    void quitFromContending();
    bool isAnImportantLock();
    void add(DetailLockData newDetailLockData);
};

//struct CriticalSectionStatus {
//    unsigned int numOfCriticalSections;
//    uint64_t totalCyclesOfCriticalSections;
//
//    void add(CriticalSectionStatus newCriticalSectionStatus);
//};

struct CacheConflictDetector {
    uint32_t numOfHitForCaches[NUM_CACHELINES_PER_PAGE];
    uint32_t numOfDifferentHitForCaches[NUM_CACHELINES_PER_PAGE];
    uint32_t totalHitIntervalForCaches[NUM_CACHELINES_PER_PAGE];
    uint32_t lastHitTimeForCaches[NUM_CACHELINES_PER_PAGE];
    unsigned long lastHitMegaIndex[NUM_CACHELINES_PER_PAGE];
    uint64_t lastHitPageIndex[NUM_CACHELINES_PER_PAGE];

    void add(CacheConflictDetector newCacheConflictDetector);
    void hit(uint64_t page_index, uint8_t cache_index, unsigned int time);
    void print(unsigned int totalHit);
};

struct FriendlinessStatus {
    unsigned int numOfSampling;
//    unsigned int numOfSampledStoringInstructions;
//    unsigned int numOfSampledCacheLines;
    unsigned int numOfFalseSharing;
    unsigned int numOfTrueSharing;
    unsigned int numThreadSwitch;

#ifdef UTIL
    uint64_t totalMemoryUsageOfSampledPages;

#ifdef CACHE_UTIL
    uint64_t totalMemoryUsageOfSampledCacheLines;
#endif

#endif

    CacheConflictDetector cacheConflictDetector;

#ifdef UTIL

#ifdef CACHE_UTIL
    void recordANewSampling(uint64_t memoryUsageOfCacheLine, uint64_t memoryUsageOfPage);
#else
    void recordANewSampling(uint64_t memoryUsageOfPage);
#endif

#else
    void recordANewSampling();
#endif

    void add(FriendlinessStatus newFriendlinessStatus);
};

constexpr char * outputTitleNotificationString[2] = {
        (char*)"\n>>>>>>>>>>>>>>>",
        (char*)"<<<<<<<<<<<<<<<\n"
};

constexpr char * allocationTypeOutputString[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {
        (char*)"serial small new malloc      ",
        (char*)"serial small reused malloc   ",
        (char*)"serial medium new malloc     ",
        (char*)"serial medium reused malloc  ",
        (char*)"serial large malloc          ",

        (char*)"parallel small new malloc    ",
        (char*)"parallel small reused malloc ",
        (char*)"parallel medium new malloc   ",
        (char*)"parallel medium reused malloc",
        (char*)"parallel large malloc        ",


        (char*)"serial small free            ",
        (char*)"serial medium free           ",
        (char*)"serial large free            ",

        (char*)"parallel small free          ",
        (char*)"parallel medium free         ",
        (char*)"parallel large free          ",


        (char*)"serial calloc                ",
        (char*)"parallel calloc              ",


        (char*)"serial realloc               ",
        (char*)"parallel realloc             ",


        (char*)"serial posix_memalign        ",
        (char*)"parallel posix_memalign      ",


        (char*)"serial memalign              ",
        (char*)"parallel memalign            "
};

constexpr char * allocationTypeOutputTitleString[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {
        (char*)"SERIAL SMALL NEW MALLOC",
        (char*)"SERIAL SMALL REUSED MALLOC",
        (char*)"SERIAL MEDIUM NEW MALLOC",
        (char*)"SERIAL MEDIUM REUSED MALLOC",
        (char*)"SERIAL LARGE MALLOC",

        (char*)"PARALLEL SMALL NEW MALLOC",
        (char*)"PARALLEL SMALL REUSED MALLOC",
        (char*)"PARALLEL MEDIUM NEW MALLOC",
        (char*)"PARALLEL MEDIUM REUSED MALLOC",
        (char*)"PARALLEL LARGE MALLOC",


        (char*)"SERIAL SMALL FREE",
        (char*)"SERIAL MEDIUM FREE",
        (char*)"SERIAL LARGE FREE",

        (char*)"PARALLEL SMALL FREE",
        (char*)"PARALLEL MEDIUM FREE",
        (char*)"PARALLEL LARGE FREE",


        (char*)"SERIAL CALLOC",
        (char*)"PARALLEL CALLOC",


        (char*)"SERIAL REALLOC",
        (char*)"PARALLEL REALLOC",


        (char*)"SERIAL POSIX_MEMALIGN",
        (char*)"PARALLEL POSIX_MEMALIGN",


        (char*)"SERIAL MEMALIGN",
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

struct CoherencyData {
    uint8_t word;
    short tid;
    uint16_t ts;
    uint16_t fs;
    uint16_t time;
    short tidsPerWord[8][16];
};

#endif //SRC_STRUCTS_H
