#include "globalstatus.h"

extern thread_local HashMap <void *, DetailLockData, nolock, PrivateHeap> lockUsage;
extern HashMap <void *, DetailLockData, nolock, PrivateHeap> globalLockUsage;

spinlock GlobalStatus::lock;
uint64_t GlobalStatus::numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
uint64_t GlobalStatus::numOfSampledCountingFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
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
        numOfSampledCountingFunctions[allocationType] += ThreadLocalStatus::numOfSampledCountingFunctions[allocationType];
        countingEvents[allocationType].add(ThreadLocalStatus::countingEvents[allocationType]);
        criticalSectionStatus[allocationType].add(ThreadLocalStatus::criticalSectionStatus[allocationType]);
        for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
            systemCallData[syscallType][allocationType].add(ThreadLocalStatus::systemCallData[syscallType][allocationType]);
        }
    }
    for(int lockType = 0; lockType < NUM_OF_LOCKTYPES; ++lockType) {
        overviewLockData[lockType].add(ThreadLocalStatus::overviewLockData[lockType]);
    }
    for(auto entryInHashTable: lockUsage) {
        void * addressOfLock = entryInHashTable.getKey();
        DetailLockData * detailLockData = globalLockUsage.find((void *)addressOfLock, sizeof(void *));
        if(detailLockData == nullptr)  {
            detailLockData = globalLockUsage.insert(addressOfLock, sizeof(void*), *(entryInHashTable.getValue()));
        } else {
            detailLockData->add(*(entryInHashTable.getValue()));
        }
    }
    friendlinessStatus.add(ThreadLocalStatus::friendlinessStatus);

    MemoryUsage::globalize();

    lock.unlock();
}

void GlobalStatus::countPotentialMemoryLeakFunctions() {
    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(allocationType == SINGAL_THREAD_NORMAL_REALLOC || allocationType == MULTI_THREAD_NORMAL_REALLOC) {
            continue;
        }
        if(allocationType == SINGAL_THREAD_SMALL_FREE || allocationType == MULTI_THREAD_SMALL_FREE
        || allocationType == SINGAL_THREAD_MEDIUM_FREE || allocationType == MULTI_THREAD_MEDIUM_FREE
        || allocationType == SINGAL_THREAD_LARGE_FREE || allocationType == MULTI_THREAD_LARGE_FREE ) {
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
    fprintf(ProgramStatus::outputFile, "(%lu)", commentNumber);
    fprintf(ProgramStatus::outputFile, "%s", outputTitleNotificationString[1]);
}

void GlobalStatus::printNumOfAllocations() {
    printTitle((char*)"ALLOCATION NUM");
    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(numOfFunctions[allocationType]) {
            fprintf(ProgramStatus::outputFile, "%s           %20lu\n", allocationTypeOutputString[allocationType], numOfFunctions[allocationType]);
        }
    }
    countPotentialMemoryLeakFunctions();
    fprintf(ProgramStatus::outputFile, "potential memory leak allocations            %20lu\n", potentialMemoryLeakFunctions);
}

void GlobalStatus::printCountingEvents() {
    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(numOfSampledCountingFunctions[allocationType]) {
            printTitle(allocationTypeOutputTitleString[allocationType], numOfSampledCountingFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "cycles           %20lu   avg %20lu\n", cycles[allocationType], cycles[allocationType]/numOfSampledCountingFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "faults           %20lu   avg %20lu\n", countingEvents[allocationType].faults, countingEvents[allocationType].faults/numOfSampledCountingFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "tlb read misses  %20lu   avg %20lu\n", countingEvents[allocationType].tlb_read_misses, countingEvents[allocationType].tlb_read_misses/numOfSampledCountingFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "tlb write misses %20lu   avg %20lu\n", countingEvents[allocationType].tlb_write_misses, countingEvents[allocationType].tlb_write_misses/numOfSampledCountingFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "cache misses     %20lu   avg %20lu\n", countingEvents[allocationType].cache_misses, countingEvents[allocationType].cache_misses/numOfSampledCountingFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "instructions     %20lu   avg %20lu\n", countingEvents[allocationType].instructions, countingEvents[allocationType].instructions/numOfSampledCountingFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "\n");
        }
    }
}

void GlobalStatus::printOverviewLocks() {
    printTitle((char*)"LOCK TOTALS");
    for(int lockType = 0; lockType < NUM_OF_LOCKTYPES; ++lockType) {
        fprintf(ProgramStatus::outputFile, "%s num                          %20u\n\n", lockTypeOutputString[lockType], overviewLockData[lockType].numOfLocks);
        if(overviewLockData[lockType].numOfLocks > 0) {
            for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
                if(overviewLockData[lockType].numOfCalls[allocationType] > 0) {
                    fprintf(ProgramStatus::outputFile, "calls in %s                %20u\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls per %s               %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCalls[allocationType]/numOfFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "contention calls in %s     %20u\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCallsWithContentions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "contention calls per %s    %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCallsWithContentions[allocationType]/numOfFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "contention rate in %s    %20u%%\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCallsWithContentions[allocationType]*100/overviewLockData[lockType].numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles in %s               %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].totalCycles[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles per lock in %s      %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].totalCycles[allocationType]/overviewLockData[lockType].numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles per %s              %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].totalCycles[allocationType]/numOfFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "\n");
                }
            }
        }
        fprintf(ProgramStatus::outputFile, "\n");
    }
}

void GlobalStatus::printDetailLocks() {
    printTitle((char*)"DETAIL LOCK USAGE");
    for(auto entryInHashTable: globalLockUsage) {
        DetailLockData * detailLockData = entryInHashTable.getValue();
        if(detailLockData->isAnImportantLock()) {
            fprintf(ProgramStatus::outputFile, "lock address %p\n", entryInHashTable.getKey());
            fprintf(ProgramStatus::outputFile, "lock type %s\n\n", lockTypeOutputString[detailLockData->lockType]);
            for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
                if(detailLockData->numOfCalls[allocationType] > 0) {
                    fprintf(ProgramStatus::outputFile, "calls in %s                  %20u\n", allocationTypeOutputString[allocationType], detailLockData->numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls per %s                 %20lu\n", allocationTypeOutputString[allocationType], detailLockData->numOfCalls[allocationType]/numOfFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls with contention in %s  %20u\n", allocationTypeOutputString[allocationType], detailLockData->numOfCallsWithContentions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls with contention per %s %20lu\n", allocationTypeOutputString[allocationType], detailLockData->numOfCallsWithContentions[allocationType]/numOfFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "contention rate in %s      %20u%%\n", allocationTypeOutputString[allocationType], detailLockData->numOfCallsWithContentions[allocationType]*100/detailLockData->numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles in %s                 %20lu\n", allocationTypeOutputString[allocationType], detailLockData->cycles[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles per lock in %s        %20lu\n", allocationTypeOutputString[allocationType], detailLockData->cycles[allocationType]/detailLockData->numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles per %s                %20lu\n", allocationTypeOutputString[allocationType], detailLockData->cycles[allocationType]/numOfFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "\n");
                }
            }
            fprintf(ProgramStatus::outputFile, "\n");
        }
    }
}

void GlobalStatus::printCriticalSections() {
    printTitle((char*)"CRITICAL SECTION");
    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(criticalSectionStatus[allocationType].numOfCriticalSections > 0) {
            fprintf(ProgramStatus::outputFile, "critical section in %s            %20u\n", allocationTypeOutputString[allocationType], criticalSectionStatus[allocationType].numOfCriticalSections);
            fprintf(ProgramStatus::outputFile, "critical section per %s           %20lu\n", allocationTypeOutputString[allocationType], criticalSectionStatus[allocationType].numOfCriticalSections/numOfFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "critical section cycles in %s     %20lu\n", allocationTypeOutputString[allocationType], criticalSectionStatus[allocationType].totalCyclesOfCriticalSections);
            fprintf(ProgramStatus::outputFile, "cycles per critical section in %s %20lu\n", allocationTypeOutputString[allocationType], criticalSectionStatus[allocationType].totalCyclesOfCriticalSections/criticalSectionStatus[allocationType].numOfCriticalSections);
            fprintf(ProgramStatus::outputFile, "critical section cycles per %s    %20lu\n", allocationTypeOutputString[allocationType], criticalSectionStatus[allocationType].totalCyclesOfCriticalSections/numOfFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "\n");
        }
    }
}

void GlobalStatus::printSyscalls() {
    printTitle((char*)"SYSCALL");
    for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
        for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
            if(systemCallData[syscallType][allocationType].num > 0) {
                fprintf(ProgramStatus::outputFile, "%s in %s                    %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].num);
                fprintf(ProgramStatus::outputFile, "%s per %s                   %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].num/numOfFunctions[allocationType]);
                fprintf(ProgramStatus::outputFile, "%s total cycles in %s       %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].cycles);
                fprintf(ProgramStatus::outputFile, "cycles per %s in %s         %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].cycles/systemCallData[syscallType][allocationType].num);
                fprintf(ProgramStatus::outputFile, "%s cycles per %s            %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].cycles/numOfFunctions[allocationType]);
                fprintf(ProgramStatus::outputFile, "\n");
            }
        }
    }
}

void GlobalStatus::printFriendliness() {
    printTitle((char*)"FRIENDLINESS");
    fprintf(ProgramStatus::outputFile, "sampling access              %20u\n", friendlinessStatus.numOfSampling);
    if(friendlinessStatus.numOfSampling / 100) {
        fprintf(ProgramStatus::outputFile, "page utilization                             %3lu%%\n",
                friendlinessStatus.totalMemoryUsageOfSampledPages/(friendlinessStatus.numOfSampling/100*PAGESIZE));
        fprintf(ProgramStatus::outputFile, "cache utilization                            %3lu%%\n",
                friendlinessStatus.totalMemoryUsageOfSampledCacheLines/(friendlinessStatus.numOfSampling/100*CACHELINE_SIZE));
        fprintf(ProgramStatus::outputFile, "accessed store instructions  %20u\n", friendlinessStatus.numOfSampledStoringInstructions);
        fprintf(ProgramStatus::outputFile, "accessed cache lines         %20u\n", friendlinessStatus.numOfSampledCacheLines);
        fprintf(ProgramStatus::outputFile, "\n");
        if(friendlinessStatus.numOfSampledStoringInstructions > 0) {
            for(int falseSharingType = 0; falseSharingType < NUM_OF_FALSESHARINGTYPE; ++falseSharingType) {
                fprintf(ProgramStatus::outputFile, "accessed %s instructions %20u %3u%%\n", falseSharingTypeOutputString[falseSharingType], friendlinessStatus.numOfSampledFalseSharingInstructions[falseSharingType], friendlinessStatus.numOfSampledFalseSharingInstructions[falseSharingType]*100/friendlinessStatus.numOfSampledStoringInstructions);
                fprintf(ProgramStatus::outputFile, "accessed %s cache lines  %20u %3u%%\n", falseSharingTypeOutputString[falseSharingType], friendlinessStatus.numOfSampledFalseSharingCacheLines[falseSharingType], friendlinessStatus.numOfSampledFalseSharingCacheLines[falseSharingType]*100/friendlinessStatus.numOfSampledCacheLines);
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

void GlobalStatus::printForMatrix() {

    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(numOfSampledCountingFunctions[allocationType]) {
            fprintf(ProgramStatus::matrixFile, "%lu ", cycles[allocationType] / numOfSampledCountingFunctions[allocationType]);
        } else {
            fprintf(ProgramStatus::matrixFile, "-1 ");
        }
    }

    for(int lockType = 0; lockType < NUM_OF_LOCKTYPES; ++lockType) {
        for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
            if(overviewLockData[lockType].numOfLocks > 0 && overviewLockData[lockType].numOfCalls[allocationType] > 0) {
                fprintf(ProgramStatus::matrixFile, "%lu ", overviewLockData[lockType].totalCycles[allocationType]/numOfFunctions[allocationType]);
            } else {
                fprintf(ProgramStatus::matrixFile, "-1 ");
            }
        }
    }

    for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
        for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
            if(systemCallData[syscallType][allocationType].num > 0) {
                fprintf(ProgramStatus::matrixFile, "%lu ", systemCallData[syscallType][allocationType].cycles/numOfFunctions[allocationType]);
            } else {
                fprintf(ProgramStatus::matrixFile, "-1 ");
            }
        }
    }

    if(MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage) {
        fprintf(ProgramStatus::matrixFile, "%lu%% ", MemoryWaste::totalValue.internalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);
        fprintf(ProgramStatus::matrixFile, "%lu%% ", MemoryWaste::totalValue.memoryBlowup*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);
        fprintf(ProgramStatus::matrixFile, "%lu%% ", MemoryWaste::totalValue.externalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);
        fprintf(ProgramStatus::matrixFile, "%lu%% ", 100 - MemoryWaste::totalValue.internalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage
                                                     - MemoryWaste::totalValue.memoryBlowup*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage
                                                     - MemoryWaste::totalValue.externalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);
        fprintf(ProgramStatus::matrixFile, "%lu ", MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage/ONE_KB);
    } else {
        fprintf(ProgramStatus::matrixFile, "-1 -1 -1 -1 -1 ");
    }

    if(friendlinessStatus.numOfSampling / 100) {
        fprintf(ProgramStatus::matrixFile, "%lu%% ", friendlinessStatus.totalMemoryUsageOfSampledPages/(friendlinessStatus.numOfSampling/100*PAGESIZE));
        fprintf(ProgramStatus::matrixFile, "%lu%% ", friendlinessStatus.totalMemoryUsageOfSampledCacheLines/(friendlinessStatus.numOfSampling/100*CACHELINE_SIZE));
    } else {
        fprintf(ProgramStatus::matrixFile, "-1 -1 ");
    }
    if(friendlinessStatus.numOfSampledStoringInstructions > 0) {
        for(int falseSharingType = 0; falseSharingType < NUM_OF_FALSESHARINGTYPE; ++falseSharingType) {
            fprintf(ProgramStatus::matrixFile, "%u%% ", friendlinessStatus.numOfSampledFalseSharingInstructions[falseSharingType]*100/friendlinessStatus.numOfSampledStoringInstructions);
            fprintf(ProgramStatus::matrixFile, "%u%% ", friendlinessStatus.numOfSampledFalseSharingCacheLines[falseSharingType]*100/friendlinessStatus.numOfSampledCacheLines);
        }
    } else {
        fprintf(ProgramStatus::matrixFile, "-1 -1 -1 -1 -1 -1\n\n");
    }
}