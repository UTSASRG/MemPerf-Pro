//
// Created by 86152 on 2020/5/31.
//

#ifndef SRC_GLOBALSTATUS_H
#define SRC_GLOBALSTATUS_H

#include <stdint.h>
#include "definevalues.h"
#include "structs.h"
#include "spinlock.hh"
#include "threadlocalstatus.h"
#include "programstatus.h"


class GlobalStatus {
private:
    static constexpr char * outoutTitleNotificationString[2] = {
            (char*)"\n>>>>>>>>>>>>>>>",
            (char*)"<<<<<<<<<<<<<<<\n"
    };

    static constexpr char * allocationTypeOutputString[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {
            (char*)"small new malloc",
            (char*)"small reused malloc",
            (char*)"large malloc",
            (char*)"small free",
            (char*)"large free",
            (char*)"calloc",
            (char*)"realloc",
            (char*)"posix_memalign",
            (char*)"memalign"
    };

    static constexpr char * allocationTypeOutputTitleString[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {
            (char*)"SMALL NEW MALLOC",
            (char*)"SMALL REUSED MALLOC",
            (char*)"LARGE MALLOC",
            (char*)"SMALL FREE",
            (char*)"LARGE FREE",
            (char*)"CALLOC",
            (char*)"REALLOC",
            (char*)"POSIX_MEMALIGN",
            (char*)"MEMALIGN"
    };

    static constexpr char * lockTypeOutputString[NUM_OF_LOCKTYPES] = {
            (char*)"mutex lock",
            (char*)"spin lock",
            (char*)"mutex try lock",
            (char*)"spin try lock"
    };

    static constexpr char * syscallTypeOutputString[NUM_OF_SYSTEMCALLTYPES] = {
            (char*)"mmap",
            (char*)"madvise",
            (char*)"sbrk",
            (char*)"mprotect",
            (char*)"munmap",
            (char*)"mremap"
    };

    static constexpr char * falseSharingTypeOutputString[NUM_OF_FALSESHARINGTYPE] = {
            (char*)"object false sharing",
            (char*)"active false sharing",
            (char*)"passive false sharing"
    };

public:
    static spinlock lock;
    static uint64_t numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static PerfReadInfo countingEvents[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static OverviewLockData overviewLockData[NUM_OF_LOCKTYPES];
    static CriticalSectionStatus criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
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

#endif //SRC_GLOBALSTATUS_H
