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
#ifdef OPEN_COUNTING_EVENT
thread_local PerfReadInfo AllocatingStatus::countingDataBeforeRealFunction;
thread_local PerfReadInfo AllocatingStatus::countingDataAfterRealFunction;
thread_local PerfReadInfo AllocatingStatus::countingDataInRealFunction;
#endif
thread_local LockTypes AllocatingStatus::nowRunningLockType;
thread_local AllocatingStatus::QueueOfDetailLockDataInAllocatingStatus AllocatingStatus::queueOfDetailLockData;
thread_local AllocatingStatus::OverviewLockDataInAllocatingStatus AllocatingStatus::overviewLockData[NUM_OF_LOCKTYPES];
thread_local AllocatingStatus::CriticalSectionStatusInAllocatingStatus AllocatingStatus::criticalSectionStatus;
thread_local SystemCallData AllocatingStatus::systemCallData[NUM_OF_SYSTEMCALLTYPES];
#ifdef OPEN_DEBUG
spinlock AllocatingStatus::debugLock;
#endif

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
#ifdef OPEN_DEBUG
    queue[queueTail].debugMutexAddress = nullptr;
#endif
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


void AllocatingStatus::startCountCountingEvents() {
#ifdef OPEN_COUNTING_EVENT
    if(sampledForCountingEvent) {
        getPerfCounts(&countingDataBeforeRealFunction);
    }
#endif
    cyclesBeforeRealFunction = rdtscp();
}

void AllocatingStatus::updateAllocatingStatusBeforeRealFunction(AllocationFunction allocationFunction, unsigned int objectSize) {
    updateAllocatingTypeBeforeRealFunction(allocationFunction, objectSize);
    if(objectSize < 64*ONE_KB) {
        sampledForCountingEvent = ThreadLocalStatus::randomProcessForCountingEvent();
    } else {
        sampledForCountingEvent = ThreadLocalStatus::randomProcessForLargeCountingEvent();
    }
    startCountCountingEvents();
}

void AllocatingStatus::updateFreeingStatusBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress) {
    updateFreeingTypeBeforeRealFunction(allocationFunction, objectAddress);
    AllocatingStatus::updateMemoryStatusBeforeFree();
    if(allocationFunction == FREE) {
        if(allocatingType.objectSize < 64*ONE_KB) {
            sampledForCountingEvent = ThreadLocalStatus::randomProcessForCountingEvent();
        } else {
            sampledForCountingEvent = ThreadLocalStatus::randomProcessForLargeCountingEvent();
        }
        startCountCountingEvents();
    }
}


void AllocatingStatus::updateAllocatingTypeAfterRealFunction(void * objectAddress) {
    allocatingType.objectAddress = objectAddress;
}

void AllocatingStatus::updateFreeingTypeAfterRealFunction() {
    allocatingType.doingAllocation = false;
}

#ifdef OPEN_COUNTING_EVENT
void AllocatingStatus::calculateCountingDataInRealFunction() {
    countingDataInRealFunction.faults = countingDataAfterRealFunction.faults - countingDataBeforeRealFunction.faults;
    countingDataInRealFunction.cache = countingDataAfterRealFunction.cache - countingDataBeforeRealFunction.cache;
    countingDataInRealFunction.instructions = countingDataAfterRealFunction.instructions - countingDataBeforeRealFunction.instructions;
}

void AllocatingStatus::removeAbnormalCountingEventValues() {
    if(countingDataInRealFunction.faults > ABNORMAL_VALUE) {
        countingDataInRealFunction.faults = 0;
    }
    if(countingDataInRealFunction.cache > ABNORMAL_VALUE) {
        countingDataInRealFunction.cache = 0;
    }
    if(countingDataInRealFunction.instructions > ABNORMAL_VALUE) {
        countingDataInRealFunction.instructions = 0;
    }
}
#endif

void AllocatingStatus::calculateCycleInRealFunction() {
    cyclesInRealFunction = cyclesAfterRealFunction - cyclesBeforeRealFunction;
    if(cyclesInRealFunction > cyclesMinus) {
        cyclesInRealFunction -= cyclesMinus;
    } else {
        cyclesInRealFunction = 0;
    }
    cyclesMinus = 0;
}

void AllocatingStatus::removeAbnormalCycleValues() {
    if(cyclesInRealFunction > ABNORMAL_VALUE) {
        cyclesInRealFunction = 0;
    }
}

void AllocatingStatus::stopCountCountingEvents() {
    cyclesAfterRealFunction = rdtscp();
#ifdef OPEN_COUNTING_EVENT
    if(sampledForCountingEvent) {
        getPerfCounts(&countingDataAfterRealFunction);
    }
#endif
    calculateCycleInRealFunction();
    removeAbnormalCycleValues();
    if(sampledForCountingEvent) {
#ifdef OPEN_COUNTING_EVENT
        calculateCountingDataInRealFunction();
        removeAbnormalCountingEventValues();
#endif
    }
}

void AllocatingStatus::updateAllocatingStatusAfterRealFunction(void * objectAddress) {
    stopCountCountingEvents();
    if(allocatingType.allocatingFunction == MALLOC || allocatingType.allocatingFunction == REALLOC) {
        updateAllocatingTypeAfterRealFunction(objectAddress);
        updateMemoryStatusAfterAllocation();
    }
    allocatingType.doingAllocation = false;
}

void AllocatingStatus::updateFreeingStatusAfterRealFunction() {
    stopCountCountingEvents();
    updateFreeingTypeAfterRealFunction();
}

void AllocatingStatus::updateMemoryStatusAfterAllocation() {

    uint8_t callsiteKey = 0;
#ifdef OPEN_BACKTRACE
        callsiteKey = Backtrace::doABackTrace(allocatingType.objectSize);
#endif
    allocatingType.allocatingTypeGotFromMemoryWaste = MemoryWaste::allocUpdate(allocatingType.objectSize, allocatingType.objectAddress, callsiteKey);
    allocatingType.allocatingTypeGotFromShadowMemory.objectNewTouchedPageSize = ShadowMemory::updateObject(allocatingType.objectAddress, allocatingType.objectSize, false);
    MemoryUsage::addToMemoryUsage(allocatingType.objectSize, allocatingType.allocatingTypeGotFromShadowMemory.objectNewTouchedPageSize);
}

void AllocatingStatus::updateMemoryStatusBeforeFree() {
    allocatingType.switchFreeingTypeGotFromMemoryWaste(MemoryWaste::freeUpdate(allocatingType.objectAddress));
    if(allocatingType.objectSize) {
        allocatingType.allocatingTypeGotFromShadowMemory.objectNewTouchedPageSize = ShadowMemory::updateObject(allocatingType.objectAddress, allocatingType.objectSize, true);
        MemoryUsage::subRealSizeFromMemoryUsage(allocatingType.objectSize);
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
            if(allocatingType.allocatingTypeGotFromMemoryWaste.isReusedObject) {
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
            if(allocatingType.allocatingTypeGotFromMemoryWaste.isReusedObject) {
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
            if(allocatingType.allocatingTypeGotFromMemoryWaste.isReusedObject) {
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
            if(allocatingType.allocatingTypeGotFromMemoryWaste.isReusedObject) {
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
        if(allocatingType.objectSize < 64*ONE_KB) {
            for(uint8_t i = 0; i < 20; ++i) {
                addUpOtherFunctionsInfoToThreadLocalData();
                addUpCountingEventsToThreadLocalData();
            }
        } else {
            addUpOtherFunctionsInfoToThreadLocalData();
            addUpCountingEventsToThreadLocalData();
        }
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
#ifdef OPEN_COUNTING_EVENT
    ThreadLocalStatus::countingEvents[allocationTypeForOutputData].add(countingDataInRealFunction);
#endif
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