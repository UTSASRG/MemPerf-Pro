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

public:
    static spinlock lock;
    static unsigned int numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static unsigned int numOfSampledCountingFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
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
    static void printFriendliness();
    static void printOutput();
//    static void printForMatrix();
};

#endif //SRC_GLOBALSTATUS_H
