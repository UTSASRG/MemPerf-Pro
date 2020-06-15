#include "globalstatus.h"

extern HashMap <void *, DetailLockData, spinlock, PrivateHeap> lockUsage;

spinlock GlobalStatus::lock;
uint64_t GlobalStatus::numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
PerfReadInfo GlobalStatus::countingEvents[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
uint64_t GlobalStatus::cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
OverviewLockData GlobalStatus::overviewLockData[NUM_OF_LOCKTYPES];
CriticalSectionStatus GlobalStatus::criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
SystemCallData GlobalStatus::systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
FriendlinessStatus GlobalStatus::friendlinessStatus;
int64_t GlobalStatus::potentialMemoryLeakFunctions;


void GlobalStatus::globalize() {
    lock.lock();
    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        cycles[allocationType] += ThreadLocalStatus::cycles[allocationType];
        numOfFunctions[allocationType] += ThreadLocalStatus::numOfFunctions[allocationType];
        countingEvents[allocationType].add(ThreadLocalStatus::countingEvents[allocationType]);
        criticalSectionStatus[allocationType].add(ThreadLocalStatus::criticalSectionStatus[allocationType]);
        for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
            systemCallData[syscallType][allocationType].add(ThreadLocalStatus::systemCallData[syscallType][allocationType]);
        }
    }
    for(int lockType = 0; lockType < NUM_OF_LOCKTYPES; ++lockType) {
        overviewLockData[lockType].add(ThreadLocalStatus::overviewLockData[lockType]);
    }
    friendlinessStatus.add(ThreadLocalStatus::friendlinessStatus);
    lock.unlock();
}

void GlobalStatus::countPotentialMemoryLeakFunctions() {
    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(allocationType == SMALL_FREE || allocationType == LARGE_FREE) {
            potentialMemoryLeakFunctions -= numOfFunctions[allocationType];
        } else {
            potentialMemoryLeakFunctions += numOfFunctions[allocationType];
        }
    }
    if(potentialMemoryLeakFunctions < 0) {
        potentialMemoryLeakFunctions = 0;
    }
}

void GlobalStatus::printTitle(char *content) {
    fprintf(ProgramStatus::outputFile, "%s", outputTitleNotificationString[0]);
    fprintf(ProgramStatus::outputFile, "%s", content);
    fprintf(ProgramStatus::outputFile, "%s", outputTitleNotificationString[1]);
}

void GlobalStatus::printTitle(char *content, uint64_t commentNumber) {
    fprintf(ProgramStatus::outputFile, "%s", outputTitleNotificationString[0]);
    fprintf(ProgramStatus::outputFile, "%s", content);
    fprintf(ProgramStatus::outputFile, "(%20lu)", commentNumber);
    fprintf(ProgramStatus::outputFile, "%s", outputTitleNotificationString[1]);
}

void GlobalStatus::printNumOfAllocations() {
    printTitle((char*)"ALLOCATION NUM");
    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        fprintf(ProgramStatus::outputFile, "%s %20lu\n", allocationTypeOutputString[allocationType], numOfFunctions[allocationType]);
    }
    countPotentialMemoryLeakFunctions();
    fprintf(ProgramStatus::outputFile, "potential memory leak allocations %20lu\n", potentialMemoryLeakFunctions);
}

void GlobalStatus::printCountingEvents() {
    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        printTitle(allocationTypeOutputTitleString[allocationType], numOfFunctions[allocationType]);
        if(numOfFunctions[allocationType]) {
            fprintf(ProgramStatus::outputFile, "cycles %20lu avg %20lu\n", cycles[allocationType], cycles[allocationType]/numOfFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "faults %20lu avg %20lu\n", countingEvents[allocationType].faults, countingEvents[allocationType].faults/numOfFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "tlb read misses %20lu avg %20lu\n", countingEvents[allocationType].tlb_read_misses, countingEvents[allocationType].tlb_read_misses/numOfFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "tlb write misses %20lu avg %20lu\n", countingEvents[allocationType].tlb_write_misses, countingEvents[allocationType].tlb_write_misses/numOfFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "cache misses %20lu avg %20lu\n", countingEvents[allocationType].cache_misses, countingEvents[allocationType].cache_misses/numOfFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "instructions %20lu avg %20lu\n", countingEvents[allocationType].instructions, countingEvents[allocationType].instructions/numOfFunctions[allocationType]);
        }
        fprintf(ProgramStatus::outputFile, "\n");
    }
}

void GlobalStatus::printOverviewLocks() {
    printTitle((char*)"LOCK TOTALS");
    for(int lockType = 0; lockType < NUM_OF_LOCKTYPES; ++lockType) {
        fprintf(ProgramStatus::outputFile, "%s num %20u\n", lockTypeOutputString[lockType], overviewLockData[lockType].numOfLocks);
        if(overviewLockData[lockType].numOfLocks > 0) {
            for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
                if(overviewLockData[lockType].numOfCalls[allocationType] > 0) {
                    fprintf(ProgramStatus::outputFile, "calls in %s %20u\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls per %s %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCalls[allocationType]/numOfFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "contention calls in %s %20u\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCallsWithContentions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "contention calls per %s %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCallsWithContentions[allocationType]/numOfFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "lock cycles in %s %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].totalCycles[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles per lock in %s %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].totalCycles[allocationType]/overviewLockData[lockType].numOfCalls[allocationType]);
                }
            }
        }
        fprintf(ProgramStatus::outputFile, "\n");
    }
}

void GlobalStatus::printDetailLocks() {
    printTitle((char*)"DETAIL LOCK USAGE");
    for(auto entryInHashTable: lockUsage) {
        DetailLockData * detailLockData = entryInHashTable.getValue();
        if(detailLockData->isAnImportantLock()) {
            fprintf(ProgramStatus::outputFile, "lock address %p\n", entryInHashTable.getKey());
            fprintf(ProgramStatus::outputFile, "lock type %s\n", lockTypeOutputString[detailLockData->lockType]);
//            fprintf(ProgramStatus::outputFile, "max contending threads %20u\n", detailLockData->maxNumOfContendingThreads);
            for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
                if(detailLockData->numOfCalls[allocationType] > 0) {
                    fprintf(ProgramStatus::outputFile, "calls in %s %20u\n", allocationTypeOutputString[allocationType], detailLockData->numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls per %s %20lu\n", allocationTypeOutputString[allocationType], detailLockData->numOfCalls[allocationType]/numOfFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls with contention in %s %20u\n", allocationTypeOutputString[allocationType], detailLockData->numOfCallsWithContentions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls with contention per %s %20lu\n", allocationTypeOutputString[allocationType], detailLockData->numOfCallsWithContentions[allocationType]/numOfFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles in %s %20lu\n", allocationTypeOutputString[allocationType], detailLockData->cycles[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles per %s %20lu\n", allocationTypeOutputString[allocationType], detailLockData->cycles[allocationType]/numOfFunctions[allocationType]);
                }
            }
            fprintf(ProgramStatus::outputFile, "\n");
        }
    }
}

void GlobalStatus::printCriticalSections() {
    printTitle((char*)"CRITICAL SECTION");
    if(criticalSectionStatus->numOfCriticalSections > 0) {
        fprintf(ProgramStatus::outputFile, "critical section %20u\n", criticalSectionStatus->numOfCriticalSections);
        fprintf(ProgramStatus::outputFile, "critical section cycles %20lu avg %20lu\n", criticalSectionStatus->totalCyclesOfCriticalSections, criticalSectionStatus->totalCyclesOfCriticalSections/criticalSectionStatus->numOfCriticalSections);
    }
}

void GlobalStatus::printSyscalls() {
    printTitle((char*)"SYSCALL");
    for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
        for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
            if(systemCallData[syscallType][allocationType].num > 0) {
                fprintf(ProgramStatus::outputFile, "%s in %s %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].num);
                fprintf(ProgramStatus::outputFile, "%s per %s %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].num/numOfFunctions[allocationType]);
                fprintf(ProgramStatus::outputFile, "%s total cycles in %s %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].cycles);
                fprintf(ProgramStatus::outputFile, "cycles per %s in %s %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].cycles/systemCallData[syscallType][allocationType].num);
                fprintf(ProgramStatus::outputFile, "\n");
            }
        }
    }
}

void GlobalStatus::printFriendliness() {
    printTitle((char*)"FRIENDLINESS");
    fprintf(ProgramStatus::outputFile, "sampling access %20u\n", friendlinessStatus.numOfSampling);
    if(friendlinessStatus.numOfSampling > 0) {
        fprintf(ProgramStatus::outputFile, "page utilization %3lu%%\n", friendlinessStatus.totalMemoryUsageOfSampledPages*100/(friendlinessStatus.numOfSampling*PAGESIZE));
        fprintf(ProgramStatus::outputFile, "cache utilization %3lu%%\n", friendlinessStatus.totalMemoryUsageOfSampledCacheLines*100/(friendlinessStatus.numOfSampling*CACHELINE_SIZE));
        fprintf(ProgramStatus::outputFile, "accessed store instructions %20u\n", friendlinessStatus.numOfSampledStoringInstructions);
        fprintf(ProgramStatus::outputFile, "accessed cache lines %20u\n", friendlinessStatus.numOfSampledCacheLines);
        fprintf(ProgramStatus::outputFile, "\n");
        if(friendlinessStatus.numOfSampledStoringInstructions > 0) {
            for(int falseSharingType = 0; falseSharingType < NUM_OF_FALSESHARINGTYPE; ++falseSharingType) {
                fprintf(ProgramStatus::outputFile, "accessed %s instructions %20u %3u%%\n", falseSharingTypeOutputString[falseSharingType], friendlinessStatus.numOfSampledFalseSharingInstructions[falseSharingType], friendlinessStatus.numOfSampledFalseSharingInstructions[falseSharingType]*100/friendlinessStatus.numOfSampledStoringInstructions);
                fprintf(ProgramStatus::outputFile, "accessed %s cache lines %20u %3u%%\n", falseSharingTypeOutputString[falseSharingType], friendlinessStatus.numOfSampledFalseSharingCacheLines[falseSharingType], friendlinessStatus.numOfSampledFalseSharingCacheLines[falseSharingType]*100/friendlinessStatus.numOfSampledCacheLines);
                fprintf(ProgramStatus::outputFile, "\n");
            }
        }
    }
}

void GlobalStatus::printOutput() {
    fprintf(stderr, "writing output file.....\n");
    ProgramStatus::printOutput();
    printNumOfAllocations();
    printCountingEvents();
    printOverviewLocks();
    printDetailLocks();
    printCriticalSections();
    printSyscalls();
    MemoryUsage::printOutput();
    MemoryWaste::printOutput();
    printFriendliness();
    fflush(ProgramStatus::outputFile);
    fprintf(stderr, "writing completed\n");
}