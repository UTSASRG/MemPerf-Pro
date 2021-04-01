#include "allocatingstatus.h"

thread_local bool AllocatingStatus::firstAllocation;
thread_local AllocatingType AllocatingStatus::allocatingType;
thread_local AllocationTypeForOutputData AllocatingStatus::allocationTypeForOutputData;
thread_local AllocationTypeForOutputData AllocatingStatus::allocationTypeForPrediction;
thread_local bool AllocatingStatus::sampledForCountingEvent;
thread_local uint64_t AllocatingStatus::cyclesBeforeRealFunction;
thread_local uint64_t AllocatingStatus::cyclesAfterRealFunction;
thread_local uint64_t AllocatingStatus::cyclesInRealFunction;
thread_local uint64_t AllocatingStatus::cyclesMinus;
thread_local LockTypes AllocatingStatus::nowRunningLockType;
thread_local AllocatingStatus::QueueOfDetailLockDataInAllocatingStatus AllocatingStatus::queueOfDetailLockData;
thread_local AllocatingStatus::OverviewLockDataInAllocatingStatus AllocatingStatus::overviewLockData[NUM_OF_LOCKTYPES];
thread_local AllocatingStatus::CriticalSectionStatusInAllocatingStatus AllocatingStatus::criticalSectionStatus;
thread_local SystemCallData AllocatingStatus::systemCallData[NUM_OF_SYSTEMCALLTYPES];

bool AllocatingStatus::isFirstFunction() {
    if(firstAllocation == false) {
        firstAllocation = true;
        return true;
    } else {
        return false;
    }
}


void AllocatingStatus::QueueOfDetailLockDataInAllocatingStatus::writingNewDataInTheQueue(DetailLockData * addressOfHashLockData) {
    if(queueTail == LENGTH_OF_QUEUE-1) {
        writingIntoHashTable();
        cleanUpQueue();
    }
    queueTail++;
    queue[queueTail].addressOfHashLockData = addressOfHashLockData;
    queue[queueTail].numOfCalls = 0;
    queue[queueTail].numOfCallsWithContentions = 0;
    queue[queueTail].cycles = 0;

    queue[queueTail].lockTimeStamp = 0;
    queue[queueTail].unlockTimeStamp = 0;
}

void AllocatingStatus::QueueOfDetailLockDataInAllocatingStatus::cleanUpQueue() {
    queueTail = -1;
}


void AllocatingStatus::updateAllocatingTypeBeforeRealFunction(AllocationFunction allocationFunction, unsigned int objectSize) {
    allocatingType.allocatingFunction = allocationFunction;
    allocatingType.objectSize = objectSize;
    allocatingType.objectSizeType = ProgramStatus::getObjectSizeType(objectSize);
    allocatingType.doingAllocation = true;
}

void AllocatingStatus::updateFreeingTypeBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress) {
    allocatingType.allocatingFunction = allocationFunction;
    allocatingType.objectAddress = objectAddress;
    allocatingType.doingAllocation = true;
}

void AllocatingStatus::updateAllocatingStatusBeforeRealFunction(AllocationFunction allocationFunction, unsigned int objectSize) {

    updateAllocatingTypeBeforeRealFunction(allocationFunction, objectSize);
#ifdef OPEN_SAMPLING_FOR_ALLOCS
    sampledForCountingEvent = ThreadLocalStatus::randomProcessForCountingEvent();
#else
    sampledForCountingEvent = true;
#endif
    cyclesBeforeRealFunction = rdtscp();
}

void AllocatingStatus::updateFreeingStatusBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress) {

    updateFreeingTypeBeforeRealFunction(allocationFunction, objectAddress);
    updateMemoryStatusBeforeFree();
    if(allocationFunction == FREE) {
#ifdef OPEN_SAMPLING_FOR_ALLOCS
        sampledForCountingEvent = ThreadLocalStatus::randomProcessForCountingEvent();
#else
        sampledForCountingEvent = true;
#endif
        cyclesBeforeRealFunction = rdtscp();
    }
}

void AllocatingStatus::updateAllocatingStatusAfterRealFunction(void * objectAddress) {

    cyclesAfterRealFunction = rdtscp();
    cyclesInRealFunction = cyclesAfterRealFunction - cyclesBeforeRealFunction;
    if(cyclesInRealFunction > cyclesMinus) {
        cyclesInRealFunction -= cyclesMinus;
    } else {
        cyclesInRealFunction = 0;
    }
    if(cyclesInRealFunction > ABNORMAL_VALUE) {
        cyclesInRealFunction = 0;
    }

    if(allocatingType.allocatingFunction == MALLOC || allocatingType.allocatingFunction == REALLOC) {
        allocatingType.objectAddress = objectAddress;
        updateMemoryStatusAfterAllocation();
    }

    allocatingType.doingAllocation = false;
}

void AllocatingStatus::updateFreeingStatusAfterRealFunction() {

    cyclesAfterRealFunction = rdtscp();
    cyclesInRealFunction = cyclesAfterRealFunction - cyclesBeforeRealFunction;
    if(cyclesInRealFunction > cyclesMinus) {
        cyclesInRealFunction -= cyclesMinus;
    } else {
        cyclesInRealFunction = 0;
    }
    if(cyclesInRealFunction > ABNORMAL_VALUE) {
        cyclesInRealFunction = 0;
    }

    allocatingType.doingAllocation = false;
}

void AllocatingStatus::updateMemoryStatusAfterAllocation() {

    allocatingType.isReuse = ObjTable::allocUpdate(allocatingType.objectSize, allocatingType.objectAddress);
    ShadowMemory::updateObject(allocatingType.objectAddress, allocatingType.objectSize, false);
}

void AllocatingStatus::updateMemoryStatusBeforeFree() {
    allocatingType.objectSize = ObjTable::freeUpdate(allocatingType.objectAddress);
    if(allocatingType.objectSize) {
        ShadowMemory::updateObject(allocatingType.objectAddress, allocatingType.objectSize, true);
    }
}

void AllocatingStatus::setAllocationTypeForOutputData() {
    if(allocatingType.allocatingFunction == MALLOC) {

        if(allocatingType.objectSizeType == LARGE) {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForOutputData = SERIAL_LARGE_MALLOC;
            } else {
                allocationTypeForOutputData = PARALLEL_LARGE_MALLOC;
            }
        } else if(allocatingType.objectSizeType == SMALL) {
            if(allocatingType.isReuse) {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForOutputData = SERIAL_SMALL_REUSED_MALLOC;
                } else {
                    allocationTypeForOutputData = PARALLEL_SMALL_REUSED_MALLOC;
                }
            } else {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForOutputData = SERIAL_SMALL_NEW_MALLOC;
                } else {
                    allocationTypeForOutputData = PARALLEL_SMALL_NEW_MALLOC;
                }
            }
        } else {
            if(allocatingType.isReuse) {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForOutputData = SERIAL_MEDIUM_REUSED_MALLOC;
                } else {
                    allocationTypeForOutputData = PARALLEL_MEDIUM_REUSED_MALLOC;
                }
            } else {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForOutputData = SERIAL_MEDIUM_NEW_MALLOC;
                } else {
                    allocationTypeForOutputData = PARALLEL_MEDIUM_NEW_MALLOC;
                }
            }
        }

    } else if(allocatingType.allocatingFunction == FREE) {

        if(allocatingType.objectSizeType == LARGE) {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForOutputData = SERIAL_LARGE_FREE;
            } else {
                allocationTypeForOutputData = PARALLEL_LARGE_FREE;
            }
        } else if(allocatingType.objectSizeType == SMALL) {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForOutputData = SERIAL_SMALL_FREE;
            } else {
                allocationTypeForOutputData = PARALLEL_SMALL_FREE;
            }
        } else {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForOutputData = SERIAL_MEDIUM_FREE;
            } else {
                allocationTypeForOutputData = PARALLEL_MEDIUM_FREE;
            }
        }

    } else if(allocatingType.allocatingFunction == CALLOC) {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForOutputData = SERIAL_NORMAL_CALLOC;
        } else {
            allocationTypeForOutputData = PARALLEL_NORMAL_CALLOC;
        }

    } else if(allocatingType.allocatingFunction == REALLOC) {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForOutputData = SERIAL_NORMAL_REALLOC;
        } else {
            allocationTypeForOutputData = PARALLEL_NORMAL_REALLOC;
        }

    } else if(allocatingType.allocatingFunction == POSIX_MEMALIGN) {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForOutputData = SERIAL_NORMAL_POSIX_MEMALIGN;
        } else {
            allocationTypeForOutputData = PARALLEL_NORMAL_POSIX_MEMALIGN;
        }

    } else {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForOutputData = SERIAL_NORMAL_MEMALIGN;
        } else {
            allocationTypeForOutputData = PARALLEL_NORMAL_MEMALIGN;
        }

    }
}

void AllocatingStatus::setAllocationTypeForPrediction() {
    if(allocatingType.allocatingFunction == MALLOC) {

        if(allocatingType.objectSizeType > Predictor::replacedLargeObjectThreshold) {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForPrediction = SERIAL_LARGE_MALLOC;
            } else {
                allocationTypeForPrediction = PARALLEL_LARGE_MALLOC;
            }
        } else if(allocatingType.objectSizeType <= Predictor::replacedMiddleObjectThreshold) {
            if(allocatingType.isReuse) {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForPrediction = SERIAL_SMALL_REUSED_MALLOC;
                } else {
                    allocationTypeForPrediction = PARALLEL_SMALL_REUSED_MALLOC;
                }
            } else {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForPrediction = SERIAL_SMALL_NEW_MALLOC;
                } else {
                    allocationTypeForPrediction = PARALLEL_SMALL_NEW_MALLOC;
                }
            }
        } else {
            if(allocatingType.isReuse) {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForPrediction = SERIAL_MEDIUM_REUSED_MALLOC;
                } else {
                    allocationTypeForPrediction = PARALLEL_MEDIUM_REUSED_MALLOC;
                }
            } else {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForPrediction = SERIAL_MEDIUM_NEW_MALLOC;
                } else {
                    allocationTypeForPrediction = PARALLEL_MEDIUM_NEW_MALLOC;
                }
            }
        }

    } else if(allocatingType.allocatingFunction == FREE) {

        if(allocatingType.objectSizeType > Predictor::replacedLargeObjectThreshold) {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForPrediction = SERIAL_LARGE_FREE;
            } else {
                allocationTypeForPrediction = PARALLEL_LARGE_FREE;
            }
        } else if(allocatingType.objectSizeType <= Predictor::replacedMiddleObjectThreshold) {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForPrediction = SERIAL_SMALL_FREE;
            } else {
                allocationTypeForPrediction = PARALLEL_SMALL_FREE;
            }
        } else {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForPrediction = SERIAL_MEDIUM_FREE;
            } else {
                allocationTypeForPrediction = PARALLEL_MEDIUM_FREE;
            }
        }

    } else if(allocatingType.allocatingFunction == CALLOC) {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForPrediction = SERIAL_NORMAL_CALLOC;
        } else {
            allocationTypeForPrediction = PARALLEL_NORMAL_CALLOC;
        }

    } else if(allocatingType.allocatingFunction == REALLOC) {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForPrediction = SERIAL_NORMAL_REALLOC;
        } else {
            allocationTypeForPrediction = PARALLEL_NORMAL_REALLOC;
        }

    } else if(allocatingType.allocatingFunction == POSIX_MEMALIGN) {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForPrediction = SERIAL_NORMAL_POSIX_MEMALIGN;
        } else {
            allocationTypeForPrediction = PARALLEL_NORMAL_POSIX_MEMALIGN;
        }

    } else {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForPrediction = SERIAL_NORMAL_MEMALIGN;
        } else {
            allocationTypeForPrediction = PARALLEL_NORMAL_MEMALIGN;
        }

    }
}

void AllocatingStatus::addUpOverviewLockDataToThreadLocalData() {
    for(int lockType = 0; lockType < NUM_OF_LOCKTYPES; ++lockType) {
        ThreadLocalStatus::overviewLockData[lockType].numOfLocks += overviewLockData[lockType].numOfLocks;
        ThreadLocalStatus::overviewLockData[lockType].numOfCalls[allocationTypeForOutputData] += overviewLockData[lockType].numOfCalls;
        ThreadLocalStatus::overviewLockData[lockType].numOfCallsWithContentions[allocationTypeForOutputData] += overviewLockData[lockType].numOfCallsWithContentions;
        ThreadLocalStatus::overviewLockData[lockType].totalCycles[allocationTypeForOutputData] += overviewLockData[lockType].cycles;
    }
}

void AllocatingStatus::addUpDetailLockDataToHashTable() {
    queueOfDetailLockData.writingIntoHashTable();
}

void AllocatingStatus::addUpCriticalSectionDataToThreadLocalData() {
    ThreadLocalStatus::criticalSectionStatus[allocationTypeForOutputData].numOfCriticalSections += criticalSectionStatus.numOfCriticalSections;
    ThreadLocalStatus::criticalSectionStatus[allocationTypeForOutputData].totalCyclesOfCriticalSections += criticalSectionStatus.totalCyclesOfCriticalSections;
}

void AllocatingStatus::addUpLockFunctionsInfoToThreadLocalData() {
    addUpOverviewLockDataToThreadLocalData();
    addUpDetailLockDataToHashTable();
    addUpCriticalSectionDataToThreadLocalData();
}

void AllocatingStatus::cleanOverviewLockDataInAllocatingStatus() {
    for(int lockType = 0; lockType < NUM_OF_LOCKTYPES; ++lockType) {
        overviewLockData[lockType].cleanUp();
    }
}

void AllocatingStatus::cleanDetailLockDataInAllocatingStatus() {
    queueOfDetailLockData.cleanUpQueue();
}

void AllocatingStatus::cleanCriticalSectionDataInAllocatingStatus() {
    criticalSectionStatus.cleanUp();
}

void AllocatingStatus::cleanLockFunctionsInfoInAllocatingStatus() {
    cleanOverviewLockDataInAllocatingStatus();
    cleanDetailLockDataInAllocatingStatus();
    cleanCriticalSectionDataInAllocatingStatus();
}

void AllocatingStatus::addUpSyscallsInfoToThreadLocalData() {
    for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
        ThreadLocalStatus::systemCallData[syscallType][allocationTypeForOutputData].add(systemCallData[syscallType]);
    }
}

void AllocatingStatus::cleanSyscallsInfoInAllocatingStatus() {
    for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
        systemCallData[syscallType].cleanup();
    }
}

void AllocatingStatus::addUpOtherFunctionsInfoToThreadLocalData() {
    addUpLockFunctionsInfoToThreadLocalData();
    addUpSyscallsInfoToThreadLocalData();
}


void AllocatingStatus::updateAllocatingInfoToThreadLocalData() {
    setAllocationTypeForOutputData();
    ThreadLocalStatus::numOfFunctions[allocationTypeForOutputData]++;
    if(sampledForCountingEvent) {
        addUpOtherFunctionsInfoToThreadLocalData();
        addUpCountingEventsToThreadLocalData();
        cleanLockFunctionsInfoInAllocatingStatus();
        cleanSyscallsInfoInAllocatingStatus();
    }
}

void AllocatingStatus::updateAllocatingInfoToPredictor() {
    AllocatingStatus::setAllocationTypeForPrediction();
    Predictor::numOfFunctions[allocationTypeForPrediction]++;
    Predictor::functionCycles[allocationTypeForPrediction] += cyclesInRealFunction;
}


void AllocatingStatus::addUpCountingEventsToThreadLocalData() {
    ThreadLocalStatus::numOfSampledCountingFunctions[allocationTypeForOutputData]++;
    ThreadLocalStatus::cycles[allocationTypeForOutputData] += cyclesInRealFunction;
}

bool AllocatingStatus::outsideTrackedAllocation() {
    return ! allocatingType.doingAllocation;
}

void AllocatingStatus::addOneSyscallToSyscallData(SystemCallTypes systemCallTypes, uint64_t cycles) {
    systemCallData[systemCallTypes].addOneSystemCall(cycles);
}

void AllocatingStatus::recordANewLock(LockTypes lockType) {
    overviewLockData[lockType].addANewLock();
}

void AllocatingStatus::initForWritingOneLockData(LockTypes lockType, DetailLockData * addressOfHashLockData) {
    nowRunningLockType = lockType;
    queueOfDetailLockData.writingNewDataInTheQueue(addressOfHashLockData);
}

void AllocatingStatus::recordALockContention() {
    overviewLockData[nowRunningLockType].numOfCallsWithContentions++;
    queueOfDetailLockData.addAContention();
}

void AllocatingStatus::recordLockCallAndCycles(unsigned int numOfCalls, uint64_t cycles) {
    overviewLockData[nowRunningLockType].addCallAndCycles(numOfCalls, cycles);
    queueOfDetailLockData.addCallAndCycles(numOfCalls, cycles);
}

#ifdef OPEN_DEBUG
void AllocatingStatus::debugRecordMutexAddress(uint64_t lockTimeStamp, pthread_mutex_t * mutex) {
    queueOfDetailLockData.debugAddMutexAddress(lockTimeStamp, mutex);
}

void AllocatingStatus::debugRecordUnlockTimeStamp(uint64_t unlockTimeStamp, pthread_mutex_t * mutex) {
    queueOfDetailLockData.debugAddUnlockTimeStamp(unlockTimeStamp, mutex);
}

bool AllocatingStatus::debugMutexAddressInTheQueue(pthread_mutex_t * mutex) {
    return queueOfDetailLockData.debugMutexAddressInTheQueue(mutex);
}
#endif

void AllocatingStatus::checkAndStartRecordingACriticalSection() {
    criticalSectionStatus.checkAndStartRecordingACriticalSection();
}

void AllocatingStatus::checkAndStopRecordingACriticalSection() {
    criticalSectionStatus.checkAndStopRecordingACriticalSection();
}

#ifdef OPEN_DEBUG
void AllocatingStatus::debugPrint() {
    fprintf(stderr, "allocating output type = %u\n", allocationTypeForOutputData);
    for(int lockType = 0; lockType < NUM_OF_LOCKTYPES; ++lockType) {
        fprintf(stderr, "lockType = %d ", lockType);
        overviewLockData[lockType].debugPrint();
    }

    queueOfDetailLockData.debugPrint();

    for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
        fprintf(stderr, "syscallType = %d ", syscallType);
        systemCallData[syscallType].debugPrint();
    }

}

size_t AllocatingStatus::debugReturnSize() {
    return(allocatingType.objectSize);
}
#endif

void AllocatingStatus::minusCycles(uint64_t cycles) {
    cyclesMinus += cycles;
}