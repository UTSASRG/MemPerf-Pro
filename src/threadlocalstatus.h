//
// Created by 86152 on 2020/5/23.
//

#ifndef MMPROF_THREADLOCALSTATUS_H
#define MMPROF_THREADLOCALSTATUS_H

#include "recordscale.hh"

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
}

struct CriticalSectionStatus {
    unsigned int numOfCriticalSections;
    uint64_t totalCyclesOfCriticalSections;

    void add(CriticalSectionStatus newCriticalSectionStatus) {
        numOfCriticalSections += newCriticalSectionStatus.numOfCriticalSections;
        totalCyclesOfCriticalSections += newCriticalSectionStatus.totalCyclesOfCriticalSections;
    }
}

typedef enum  {
    OBJECT,
    ACTIVE,
    PASSIVE,
    NUM_OF_FALSESHARINGTYPE
} FalseSharingType;

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
        for(int falseSharingType = 0; falseSharingType < NUM_OF_FALSESHARINGTYPE; +=falseSharingType) {
            numOfSampledFalseSharingInstructions[falseSharingType] += newFriendlinessStatus.numOfSampledFalseSharingInstructions[falseSharingType];
            numOfSampledFalseSharingCacheLines[falseSharingType] += newFriendlinessStatus.numOfSampledFalseSharingCacheLines[falseSharingType];
        }
    }
};

class ThreadLocalStatus {
public:
    static unsigned int totalNumOfRunningThread;
    static thread_local unsigned int runningThreadIndex;
    static spinlock lock;

    static thread_local uint64_t numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local PerfReadInfo countingEvents[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    thread_local OverviewLockData overviewLockData[NUM_OF_LOCKTYPES];
    thread_local CriticalSectionStatus criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local SystemCallData systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local FriendlinessStatus friendlinessStatus;

    static void getARunningThreadIndex();

};

class GlobalStatus {
private:
    static const char * outoutTitleNotificationString = {
            "\n>>>>>>>>>>>>>>>",
            "<<<<<<<<<<<<<<<\n"
    }

    static const char * allocationTypeOutputString = {
            "small new malloc",
            "small reused malloc",
            "large malloc",
            "small free",
            "large free",
            "calloc",
            "realloc",
            "posix_memalign",
            "memalign"
    };

    static const char * allocationTypeOutputTitleString = {
            "SMALL NEW MALLOC",
            "SMALL REUSED MALLOC",
            "LARGE MALLOC",
            "SMALL FREE",
            "LARGE FREE",
            "CALLOC",
            "REALLOC",
            "POSIX_MEMALIGN",
            "MEMALIGN"
    };

    static const char * lockTypeOutputString = {
            "mutex lock",
            "spin lock",
            "mutex try lock",
            "spin try lock"
    };

    static const char * syscallTypeOutputString = {
            "mmap",
            "madvise",
            "sbrk",
            "mprotect",
            "munmap",
            "mremap"
    };

    static const char * falseSharingTypeOutputString = {
            "object false sharing",
            "active false sharing",
            "passive false sharing"
    };

public:
    static spinlock lock;
    static uint64_t numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static PerfReadInfo countingEvents[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    OverviewLockData overviewLockData[NUM_OF_LOCKTYPES];
    CriticalSectionStatus criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static SystemCallData systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static FriendlinessStatus friendlinessStatus;

    static int64_t potentialMemoryLeakFunctions;
    static void globalize();

    static void countPotentialMemoryLeakFunctions();
    static void printTitle(char * content);
    static void printTitle(char * content, uint64_t commentNumber);

    static void printNumOfAllocations();
    static void printCountingEvents();
    static void printOverviewLocks();
    static void printDetailLocks();
    static void printCriticalSections();
    static void printSyscalls();
    static void printMemoryUsage();
    static void printFriendliness();
    static void printOutput();
};

#endif //MMPROF_THREADLOCALSTATUS_H
