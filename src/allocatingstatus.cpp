#include "allocatingstatus.h"

extern thread_local bool PMUinit;

thread_local AllocatingType AllocatingStatus::allocatingType;
thread_local AllocationTypeForOutputData AllocatingStatus::allocationTypeForOutputData;
thread_local bool AllocatingStatus::sampledForCountingEvent;
thread_local uint64_t AllocatingStatus::cyclesBeforeRealFunction;
thread_local uint64_t AllocatingStatus::cyclesAfterRealFunction;
thread_local uint64_t AllocatingStatus::cyclesInRealFunction;
thread_local PerfReadInfo AllocatingStatus::countingDataBeforeRealFunction;
thread_local PerfReadInfo AllocatingStatus::countingDataAfterRealFunction;
thread_local PerfReadInfo AllocatingStatus::countingDataInRealFunction;
thread_local LockTypes AllocatingStatus::nowRunningLockType;
thread_local AllocatingStatus::QueueOfDetailLockDataInAllocatingStatus AllocatingStatus::queueOfDetailLockData;
thread_local AllocatingStatus::OverviewLockDataInAllocatingStatus AllocatingStatus::overviewLockData[NUM_OF_LOCKTYPES];
thread_local AllocatingStatus::CriticalSectionStatusInAllocatingStatus AllocatingStatus::criticalSectionStatus;
thread_local SystemCallData AllocatingStatus::systemCallData[NUM_OF_SYSTEMCALLTYPES];
spinlock AllocatingStatus::debugLock;

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
    queue[queueTail].debugMutexAddress = nullptr;
}

void AllocatingStatus::QueueOfDetailLockDataInAllocatingStatus::cleanUpQueue() {
    queueTail = -1;
}


void AllocatingStatus::updateAllocatingTypeBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize) {
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
    if(sampledForCountingEvent) {
        getPerfCounts(&countingDataBeforeRealFunction);
        cyclesBeforeRealFunction = rdtscp();
    }
}

void AllocatingStatus::updateAllocatingStatusBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize) {
    updateAllocatingTypeBeforeRealFunction(allocationFunction, objectSize);
    sampledForCountingEvent = ThreadLocalStatus::randomProcessForCountingEvent();
    startCountCountingEvents();
}

void AllocatingStatus::updateFreeingStatusBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress) {
    updateFreeingTypeBeforeRealFunction(allocationFunction, objectAddress);
    AllocatingStatus::updateMemoryStatusBeforeFree();
    if(allocationFunction == FREE) {
        sampledForCountingEvent = ThreadLocalStatus::randomProcessForCountingEvent();
        startCountCountingEvents();
    }
}


void AllocatingStatus::updateAllocatingTypeAfterRealFunction(void * objectAddress) {
    allocatingType.doingAllocation = false;
    allocatingType.objectAddress = objectAddress;
}

void AllocatingStatus::updateFreeingTypeAfterRealFunction() {
    allocatingType.doingAllocation = false;
}

void AllocatingStatus::calculateCountingDataInRealFunction() {
    cyclesInRealFunction = cyclesAfterRealFunction - cyclesBeforeRealFunction;
    countingDataInRealFunction.faults = countingDataAfterRealFunction.faults - countingDataBeforeRealFunction.faults;
    countingDataInRealFunction.cache_misses = countingDataAfterRealFunction.cache_misses - countingDataBeforeRealFunction.cache_misses;
    countingDataInRealFunction.tlb_read_misses = countingDataAfterRealFunction.tlb_read_misses - countingDataBeforeRealFunction.tlb_read_misses;
    countingDataInRealFunction.tlb_write_misses = countingDataAfterRealFunction.tlb_write_misses - countingDataBeforeRealFunction.tlb_write_misses;
    countingDataInRealFunction.instructions = countingDataAfterRealFunction.instructions - countingDataBeforeRealFunction.instructions;
}

void AllocatingStatus::removeAbnormalCountingEventValues() {
    if(cyclesInRealFunction > ABNORMAL_VALUE) {
        cyclesInRealFunction = 0;
    }
    if(countingDataInRealFunction.faults > ABNORMAL_VALUE) {
        countingDataInRealFunction.faults = 0;
    }
    if(countingDataInRealFunction.cache_misses > ABNORMAL_VALUE) {
        countingDataInRealFunction.cache_misses = 0;
    }
    if(countingDataInRealFunction.tlb_read_misses > ABNORMAL_VALUE) {
        countingDataInRealFunction.tlb_read_misses = 0;
    }
    if(countingDataInRealFunction.tlb_write_misses > ABNORMAL_VALUE) {
        countingDataInRealFunction.tlb_write_misses = 0;
    }
    if(countingDataInRealFunction.instructions > ABNORMAL_VALUE) {
        countingDataInRealFunction.instructions = 0;
    }
}

void AllocatingStatus::stopCountCountingEvents() {
    if(sampledForCountingEvent) {
        cyclesAfterRealFunction = rdtscp();
        getPerfCounts(&countingDataAfterRealFunction);
        calculateCountingDataInRealFunction();
        removeAbnormalCountingEventValues();
    }
}

void AllocatingStatus::updateAllocatingStatusAfterRealFunction(void * objectAddress) {
    stopCountCountingEvents();
    updateAllocatingTypeAfterRealFunction(objectAddress);
    updateMemoryStatusAfterAllocation();
}

void AllocatingStatus::updateFreeingStatusAfterRealFunction() {
    stopCountCountingEvents();
    updateFreeingTypeAfterRealFunction();
}

void AllocatingStatus::updateMemoryStatusAfterAllocation() {
    allocatingType.allocatingTypeGotFromMemoryWaste = MemoryWaste::allocUpdate(allocatingType.objectSize, allocatingType.objectAddress);
    allocatingType.allocatingTypeGotFromShadowMemory.objectNewTouchedPageSize = ShadowMemory::updateObject(allocatingType.objectAddress, allocatingType.objectSize, false);
    MemoryUsage::addToMemoryUsage(allocatingType.objectSize, allocatingType.allocatingTypeGotFromShadowMemory.objectNewTouchedPageSize);
}

void AllocatingStatus::updateMemoryStatusBeforeFree() {
    allocatingType.switchFreeingTypeGotFromMemoryWaste(MemoryWaste::freeUpdate(allocatingType.objectAddress));
    allocatingType.allocatingTypeGotFromShadowMemory.objectNewTouchedPageSize = ShadowMemory::updateObject(allocatingType.objectAddress, allocatingType.objectSize, true);
    MemoryUsage::subRealSizeFromMemoryUsage(allocatingType.objectSize);
}

void AllocatingStatus::setAllocationTypeForOutputData() {

    if(allocatingType.allocatingFunction == MALLOC) {

        if(allocatingType.objectSizeType == LARGE) {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForOutputData = SINGAL_THREAD_LARGE_MALLOC;
            } else {
                allocationTypeForOutputData = MULTI_THREAD_LARGE_MALLOC;
            }
        } else if(allocatingType.objectSizeType == SMALL) {
            if(allocatingType.allocatingTypeGotFromMemoryWaste.isReusedObject) {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForOutputData = SINGAL_THREAD_SMALL_REUSED_MALLOC;
                } else {
                    allocationTypeForOutputData = MULTI_THREAD_SMALL_REUSED_MALLOC;
                }
            } else {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForOutputData = SINGAL_THREAD_SMALL_NEW_MALLOC;
                } else {
                    allocationTypeForOutputData = MULTI_THREAD_SMALL_NEW_MALLOC;
                }
            }
        } else {
            if(allocatingType.allocatingTypeGotFromMemoryWaste.isReusedObject) {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForOutputData = SINGAL_THREAD_MEDIUM_REUSED_MALLOC;
                } else {
                    allocationTypeForOutputData = MULTI_THREAD_MEDIUM_REUSED_MALLOC;
                }
            } else {
                if(ThreadLocalStatus::isCurrentlySingleThread()) {
                    allocationTypeForOutputData = SINGAL_THREAD_MEDIUM_NEW_MALLOC;
                } else {
                    allocationTypeForOutputData = MULTI_THREAD_MEDIUM_NEW_MALLOC;
                }
            }
        }

    } else if(allocatingType.allocatingFunction == FREE) {

        if(allocatingType.objectSizeType == LARGE) {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForOutputData = SINGAL_THREAD_LARGE_FREE;
            } else {
                allocationTypeForOutputData = MULTI_THREAD_LARGE_FREE;
            }
        } else if(allocatingType.objectSizeType == SMALL) {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForOutputData = SINGAL_THREAD_SMALL_FREE;
            } else {
                allocationTypeForOutputData = MULTI_THREAD_SMALL_FREE;
            }
        } else {
            if(ThreadLocalStatus::isCurrentlySingleThread()) {
                allocationTypeForOutputData = SINGAL_THREAD_MEDIUM_FREE;
            } else {
                allocationTypeForOutputData = MULTI_THREAD_MEDIUM_FREE;
            }
        }

    } else if(allocatingType.allocatingFunction == CALLOC) {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForOutputData = SINGAL_THREAD_NORMAL_CALLOC;
        } else {
            allocationTypeForOutputData = MULTI_THREAD_NORMAL_CALLOC;
        }

    } else if(allocatingType.allocatingFunction == REALLOC) {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForOutputData = SINGAL_THREAD_NORMAL_REALLOC;
        } else {
            allocationTypeForOutputData = MULTI_THREAD_NORMAL_REALLOC;
        }

    } else if(allocatingType.allocatingFunction == POSIX_MEMALIGN) {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForOutputData = SINGAL_THREAD_NORMAL_POSIX_MEMALIGN;
        } else {
            allocationTypeForOutputData = MULTI_THREAD_NORMAL_POSIX_MEMALIGN;
        }

    } else {

        if(ThreadLocalStatus::isCurrentlySingleThread()) {
            allocationTypeForOutputData = SINGAL_THREAD_NORMAL_MEMALIGN;
        } else {
            allocationTypeForOutputData = MULTI_THREAD_NORMAL_MEMALIGN;
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
    addUpOtherFunctionsInfoToThreadLocalData();
    addUpCountingEventsToThreadLocalData();
    cleanLockFunctionsInfoInAllocatingStatus();
    cleanSyscallsInfoInAllocatingStatus();
}

void AllocatingStatus::addUpCountingEventsToThreadLocalData() {
    if(sampledForCountingEvent) {
        ThreadLocalStatus::numOfSampledCountingFunctions[allocationTypeForOutputData]++;
        ThreadLocalStatus::cycles[allocationTypeForOutputData] += cyclesInRealFunction;
        ThreadLocalStatus::countingEvents[allocationTypeForOutputData].add(countingDataInRealFunction);
    }

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

void AllocatingStatus::debugRecordMutexAddress(uint64_t lockTimeStamp, pthread_mutex_t * mutex) {
    queueOfDetailLockData.debugAddMutexAddress(lockTimeStamp, mutex);
}

void AllocatingStatus::debugRecordUnlockTimeStamp(uint64_t unlockTimeStamp, pthread_mutex_t * mutex) {
    queueOfDetailLockData.debugAddUnlockTimeStamp(unlockTimeStamp, mutex);
}

bool AllocatingStatus::debugMutexAddressInTheQueue(pthread_mutex_t * mutex) {
    return queueOfDetailLockData.debugMutexAddressInTheQueue(mutex);
}

void AllocatingStatus::checkAndStartRecordingACriticalSection() {
    criticalSectionStatus.checkAndStartRecordingACriticalSection();
}

void AllocatingStatus::checkAndStopRecordingACriticalSection() {
    criticalSectionStatus.checkAndStopRecordingACriticalSection();
}

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