#include "globalstatus.h"

extern thread_local HashMap <void *, DetailLockData, PrivateHeap> lockUsage;
extern HashMap <void *, DetailLockData, PrivateHeap> globalLockUsage;

spinlock GlobalStatus::lock;
//unsigned int GlobalStatus::numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
unsigned int GlobalStatus::numOfSampledCountingFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
uint64_t GlobalStatus::cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
OverviewLockData GlobalStatus::overviewLockData[NUM_OF_LOCKTYPES];
//CriticalSectionStatus GlobalStatus::criticalSectionStatus[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
SystemCallData GlobalStatus::systemCallData[NUM_OF_SYSTEMCALLTYPES][NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
FriendlinessStatus GlobalStatus::friendlinessStatus;
int64_t GlobalStatus::potentialMemoryLeakFunctions;


void GlobalStatus::globalize() {
    lock.lock();

    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        cycles[allocationType] += ThreadLocalStatus::cycles[allocationType];
//        numOfFunctions[allocationType] += ThreadLocalStatus::numOfFunctions[allocationType];
        numOfSampledCountingFunctions[allocationType] += ThreadLocalStatus::numOfSampledCountingFunctions[allocationType];
//        criticalSectionStatus[allocationType].add(ThreadLocalStatus::criticalSectionStatus[allocationType]);
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

    lock.unlock();
}

void GlobalStatus::countPotentialMemoryLeakFunctions() {
    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(allocationType == SERIAL_NORMAL_REALLOC || allocationType == PARALLEL_NORMAL_REALLOC) {
            continue;
        }
        if(allocationType == SERIAL_SMALL_FREE || allocationType == PARALLEL_SMALL_FREE
        || allocationType == SERIAL_MEDIUM_FREE || allocationType == PARALLEL_MEDIUM_FREE
        || allocationType == SERIAL_LARGE_FREE || allocationType == PARALLEL_LARGE_FREE ) {
            potentialMemoryLeakFunctions -= numOfSampledCountingFunctions[allocationType];
        } else {
            potentialMemoryLeakFunctions += numOfSampledCountingFunctions[allocationType];
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
    for(uint8_t allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(numOfSampledCountingFunctions[allocationType]) {
            fprintf(ProgramStatus::outputFile, "%s           %20u\n", allocationTypeOutputString[allocationType], numOfSampledCountingFunctions[allocationType]);
        }
    }
    countPotentialMemoryLeakFunctions();
    fprintf(ProgramStatus::outputFile, "potential memory leak allocations            %20lu\n", potentialMemoryLeakFunctions);
}

void GlobalStatus::printCountingEvents() {
    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(numOfSampledCountingFunctions[allocationType]) {
            printTitle(allocationTypeOutputTitleString[allocationType], numOfSampledCountingFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "cycles                %20lu   avg %20lu\n", cycles[allocationType], cycles[allocationType]/numOfSampledCountingFunctions[allocationType]);
            fprintf(ProgramStatus::outputFile, "\n");
        }
    }
}

void GlobalStatus::printOverviewLocks() {
    printTitle((char*)"LOCK TOTALS");
    for(int lockType = 0; lockType < NUM_OF_LOCKTYPES; ++lockType) {
//        fprintf(ProgramStatus::outputFile, "%s num                          %20u\n\n", lockTypeOutputString[lockType], overviewLockData[lockType].numOfLocks);
        if(overviewLockData[lockType].numOfLocks > 0) {
            for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
                if(overviewLockData[lockType].numOfCalls[allocationType] > 0) {
                    fprintf(ProgramStatus::outputFile, "calls in %s                %20u\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls per %s               %20.1lf\n", allocationTypeOutputString[allocationType], (double)overviewLockData[lockType].numOfCalls[allocationType]/(double)numOfSampledCountingFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "contention calls in %s     %20u\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCallsWithContentions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "contention calls per %s    %20.1lf\n", allocationTypeOutputString[allocationType], (double)overviewLockData[lockType].numOfCallsWithContentions[allocationType]/(double)numOfSampledCountingFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "contention rate in %s    %20u%%\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].numOfCallsWithContentions[allocationType]*100/overviewLockData[lockType].numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles in %s               %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].totalCycles[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles per lock in %s      %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].totalCycles[allocationType]/overviewLockData[lockType].numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles per %s              %20lu\n", allocationTypeOutputString[allocationType], overviewLockData[lockType].totalCycles[allocationType]/numOfSampledCountingFunctions[allocationType]);
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
                    fprintf(ProgramStatus::outputFile, "calls per %s                 %20.1lf\n", allocationTypeOutputString[allocationType], (double)detailLockData->numOfCalls[allocationType]/(double)numOfSampledCountingFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls with contention in %s  %20u\n", allocationTypeOutputString[allocationType], detailLockData->numOfCallsWithContentions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "calls with contention per %s %20.1lf\n", allocationTypeOutputString[allocationType], (double)detailLockData->numOfCallsWithContentions[allocationType]/(double)numOfSampledCountingFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "contention rate in %s      %20u%%\n", allocationTypeOutputString[allocationType], detailLockData->numOfCallsWithContentions[allocationType]*100/detailLockData->numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles in %s                 %20lu\n", allocationTypeOutputString[allocationType], detailLockData->cycles[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles per lock in %s        %20lu\n", allocationTypeOutputString[allocationType], detailLockData->cycles[allocationType]/detailLockData->numOfCalls[allocationType]);
                    fprintf(ProgramStatus::outputFile, "cycles per %s                %20lu\n", allocationTypeOutputString[allocationType], detailLockData->cycles[allocationType]/numOfSampledCountingFunctions[allocationType]);
                    fprintf(ProgramStatus::outputFile, "\n");
                }
            }
            fprintf(ProgramStatus::outputFile, "\n");
        }
    }
}

//void GlobalStatus::printCriticalSections() {
//    printTitle((char*)"CRITICAL SECTION");
//    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
//        if(criticalSectionStatus[allocationType].numOfCriticalSections > 0) {
//            fprintf(ProgramStatus::outputFile, "critical section in %s            %20u\n", allocationTypeOutputString[allocationType], criticalSectionStatus[allocationType].numOfCriticalSections);
//            fprintf(ProgramStatus::outputFile, "critical section per %s           %20.1lf\n", allocationTypeOutputString[allocationType], (double)criticalSectionStatus[allocationType].numOfCriticalSections/(double)numOfSampledCountingFunctions[allocationType]);
//            fprintf(ProgramStatus::outputFile, "critical section cycles in %s     %20lu\n", allocationTypeOutputString[allocationType], criticalSectionStatus[allocationType].totalCyclesOfCriticalSections);
//            fprintf(ProgramStatus::outputFile, "cycles per critical section in %s %20lu\n", allocationTypeOutputString[allocationType], criticalSectionStatus[allocationType].totalCyclesOfCriticalSections/criticalSectionStatus[allocationType].numOfCriticalSections);
//            fprintf(ProgramStatus::outputFile, "critical section cycles per %s    %20lu\n", allocationTypeOutputString[allocationType], criticalSectionStatus[allocationType].totalCyclesOfCriticalSections/numOfSampledCountingFunctions[allocationType]);
//            fprintf(ProgramStatus::outputFile, "\n");
//        }
//    }
//}

void GlobalStatus::printSyscalls() {
    printTitle((char*)"SYSCALL");
    for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
        for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
            if(systemCallData[syscallType][allocationType].num > 0) {
                fprintf(ProgramStatus::outputFile, "%s in %s                    %20u\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].num);
                fprintf(ProgramStatus::outputFile, "%s per %s                   %20.1lf\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], (double)systemCallData[syscallType][allocationType].num/(double)numOfSampledCountingFunctions[allocationType]);
                fprintf(ProgramStatus::outputFile, "%s total cycles in %s       %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].cycles);
                fprintf(ProgramStatus::outputFile, "cycles per %s in %s         %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].cycles/systemCallData[syscallType][allocationType].num);
                fprintf(ProgramStatus::outputFile, "%s cycles per %s            %20lu\n", syscallTypeOutputString[syscallType], allocationTypeOutputString[allocationType], systemCallData[syscallType][allocationType].cycles/numOfSampledCountingFunctions[allocationType]);
                fprintf(ProgramStatus::outputFile, "\n");
            }
        }
    }
}

void GlobalStatus::printFriendliness() {
#ifdef OPEN_SAMPLING_EVENT
    printTitle((char*)"FRIENDLINESS");
    fprintf(ProgramStatus::outputFile, "sampling access              %20u\n", friendlinessStatus.numOfSampling);
    if(friendlinessStatus.numOfSampling / 100) {

#ifdef UTIL
        fprintf(ProgramStatus::outputFile, "page utilization                             %3lu%%\n",
                friendlinessStatus.totalMemoryUsageOfSampledPages*100/friendlinessStatus.numOfSampling/PAGESIZE);

#ifdef CACHE_UTIL
        fprintf(ProgramStatus::outputFile, "cache utilization                            %3lu%%\n",
                friendlinessStatus.totalMemoryUsageOfSampledCacheLines*100/friendlinessStatus.numOfSampling/CACHELINE_SIZE);
#endif

#endif

//        fprintf(ProgramStatus::outputFile, "accessed store instructions  %20u\n", friendlinessStatus.numOfSampledStoringInstructions);
//        fprintf(ProgramStatus::outputFile, "accessed cache lines         %20u\n", friendlinessStatus.numOfSampledCacheLines);
        fprintf(ProgramStatus::outputFile, "\n");
        if(friendlinessStatus.numThreadSwitch > 0) {
            fprintf(ProgramStatus::outputFile, "true sharing instructions %20u %3u%%\n", friendlinessStatus.numOfTrueSharing, friendlinessStatus.numOfTrueSharing*100/friendlinessStatus.numThreadSwitch);
            fprintf(ProgramStatus::outputFile, "false sharing instructions %20u %3u%%\n", friendlinessStatus.numOfFalseSharing, friendlinessStatus.numOfFalseSharing*100/friendlinessStatus.numThreadSwitch);
            fprintf(ProgramStatus::outputFile, "\n");
            if(friendlinessStatus.numOfTrueSharing*100/friendlinessStatus.numThreadSwitch > 15|| friendlinessStatus.numOfFalseSharing*100/friendlinessStatus.numThreadSwitch > 15) {
                ShadowMemory::printOutput();
            }
        }

    }
    friendlinessStatus.cacheConflictDetector.print(friendlinessStatus.numOfSampling);
#endif
}

void GlobalStatus::printOutput() {
    fprintf(stderr, "writing output file.....\n");
//    fprintf(stderr, "%d threads\n", ThreadLocalStatus::maxNumOfRunningThread);
//    fprintf(stderr, "%d threads\n", ThreadLocalStatus::totalNumOfThread);
    ProgramStatus::printOutput();
    printNumOfAllocations();
    printCountingEvents();
    printOverviewLocks();
    printDetailLocks();
//    printCriticalSections();
    printSyscalls();
    printFriendliness();
#ifdef PREDICTION
    Predictor::printOutput();
#endif

//    fflush(ProgramStatus::outputFile);
    fprintf(stderr, "writing completed\n");
}

//void GlobalStatus::printForMatrix() {
//
//    if(!ProgramStatus::matrixFileOpened) {
//        return;
//    }
//
//    for(int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
//        if(allocationType == PARALLEL_MEDIUM_FREE || allocationType == PARALLEL_MEDIUM_NEW_MALLOC || allocationType == PARALLEL_MEDIUM_REUSED_MALLOC
//           || allocationType == SERIAL_MEDIUM_FREE || allocationType == SERIAL_MEDIUM_NEW_MALLOC || allocationType == SERIAL_MEDIUM_REUSED_MALLOC
//           || allocationType == SERIAL_NORMAL_CALLOC || allocationType == SERIAL_NORMAL_REALLOC || allocationType == SERIAL_NORMAL_POSIX_MEMALIGN || allocationType == SERIAL_NORMAL_MEMALIGN
//           || allocationType == PARALLEL_NORMAL_CALLOC || allocationType == PARALLEL_NORMAL_REALLOC || allocationType == PARALLEL_NORMAL_POSIX_MEMALIGN || allocationType == PARALLEL_NORMAL_MEMALIGN) {
//            continue;
//        }
//
////        if(allocationType != SERIAL_MEDIUM_NEW_MALLOC && allocationType != SERIAL_MEDIUM_REUSED_MALLOC && allocationType != SERIAL_MEDIUM_FREE &&
////        allocationType != PARALLEL_MEDIUM_NEW_MALLOC && allocationType != PARALLEL_MEDIUM_REUSED_MALLOC && allocationType != PARALLEL_MEDIUM_FREE) {
////            continue;
////        }
//
//        fprintf(ProgramStatus::matrixFile, "%u ", numOfSampledCountingFunctions[allocationType]);
//        if(numOfSampledCountingFunctions[allocationType]) {
//            fprintf(ProgramStatus::matrixFile, "%lu ", cycles[allocationType] / numOfSampledCountingFunctions[allocationType]);
//        } else {
//            fprintf(ProgramStatus::matrixFile, "-1 ");
//        }
//
//        uint64_t totalCyclesFromLocks = 0;
//        for(int lockType = 0; lockType < NUM_OF_LOCKTYPES; ++lockType) {
//            if(overviewLockData[lockType].numOfLocks > 0 && overviewLockData[lockType].numOfCalls[allocationType] > 0) {
//                totalCyclesFromLocks += overviewLockData[lockType].totalCycles[allocationType]/numOfSampledCountingFunctions[allocationType];
//            }
//        }
//        fprintf(ProgramStatus::matrixFile, "%lu ", totalCyclesFromLocks);
//        uint64_t totalCyclesFromSyscalls = 0;
//        for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
//            if(systemCallData[syscallType][allocationType].num > 0) {
//                totalCyclesFromSyscalls += systemCallData[syscallType][allocationType].cycles/numOfSampledCountingFunctions[allocationType];
//            }
//        }
//        fprintf(ProgramStatus::matrixFile, "%lu ", totalCyclesFromSyscalls);
//    }
////
//    if(MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage) {
//        fprintf(ProgramStatus::matrixFile, "%lu%% ", ObjTable::totalValue.internalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);
//        fprintf(ProgramStatus::matrixFile, "%lu%% ", ObjTable::totalValue.memoryBlowup*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);
//        fprintf(ProgramStatus::matrixFile, "%lu%% ", ObjTable::totalValue.externalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);
//        fprintf(ProgramStatus::matrixFile, "%lu%% ", 100 - ObjTable::totalValue.internalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage
//                                                     - ObjTable::totalValue.memoryBlowup*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage
//                                                     - ObjTable::totalValue.externalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);
//        fprintf(ProgramStatus::matrixFile, "%lu ", MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage/ONE_KB);
//    } else {
//        fprintf(ProgramStatus::matrixFile, "-1 -1 -1 -1 -1 ");
//    }
//
//    if(friendlinessStatus.numOfSampling / 100) {
//        fprintf(ProgramStatus::matrixFile, "%lu%% ", friendlinessStatus.totalMemoryUsageOfSampledPages/(friendlinessStatus.numOfSampling/100*PAGESIZE));
//        fprintf(ProgramStatus::matrixFile, "%lu%% ", friendlinessStatus.totalMemoryUsageOfSampledCacheLines/(friendlinessStatus.numOfSampling/100*CACHELINE_SIZE));
//    } else {
//        fprintf(ProgramStatus::matrixFile, "-1 -1 ");
//    }
//    if(friendlinessStatus.numOfSampledStoringInstructions > 0) {
//        for(int falseSharingType = 0; falseSharingType < NUM_OF_FALSESHARINGTYPE; ++falseSharingType) {
//            fprintf(ProgramStatus::matrixFile, "%u%% ", friendlinessStatus.numOfSampledFalseSharingInstructions[falseSharingType]*100/friendlinessStatus.numOfSampledStoringInstructions);
//            fprintf(ProgramStatus::matrixFile, "%u%% ", friendlinessStatus.numOfSampledFalseSharingCacheLines[falseSharingType]*100/friendlinessStatus.numOfSampledCacheLines);
//        }
//    } else {
//        fprintf(ProgramStatus::matrixFile, "-1 -1 -1 -1 -1 -1");
//    }
//
//    fprintf(ProgramStatus::matrixFile, "%lu ", Predictor::criticalCycle);
//    fprintf(ProgramStatus::matrixFile, "%lu ", Predictor::replacedCriticalCycle);
//    if(Predictor::replacedCriticalCycle) {
//        fprintf(ProgramStatus::matrixFile, "%3lu%% ", Predictor::criticalCycle*100/Predictor::replacedCriticalCycle);
//    } else {
//        fprintf(ProgramStatus::matrixFile, "100%% ");
//    }
//    if(Predictor::replacedCriticalCycle && Predictor::replacedCriticalCycle < Predictor::criticalCycle) {
//        fprintf(ProgramStatus::matrixFile, "%3lu%% ", (Predictor::criticalCycle-Predictor::replacedCriticalCycle)*100/Predictor::replacedCriticalCycle);
//    } else {
//        fprintf(ProgramStatus::matrixFile, "0%% ");
//    }
//
//    fprintf(ProgramStatus::matrixFile, "\n");
//}