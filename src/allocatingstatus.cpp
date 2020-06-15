#include "allocatingstatus.h"

extern thread_local bool PMUinit;

thread_local AllocatingType AllocatingStatus::allocatingType;
thread_local AllocationTypeForOutputData AllocatingStatus::allocationTypeForOutputData;
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
    queueTail++;
    if(queueTail >= 100) {
        fprintf(stderr, "Queue Of detail lock data used up\n");
        abort();
    }
    queue[queueTail].addressOfHashLockData = addressOfHashLockData;
    queue[queueTail].numOfCalls = 0;
    queue[queueTail].numOfCallsWithContentions = 0;
    queue[queueTail].cycles = 0;
}

void AllocatingStatus::QueueOfDetailLockDataInAllocatingStatus::cleanUpQueue() {
    queueTail = -1;
}


void AllocatingStatus::updateAllocatingTypeBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize) {
    allocatingType.allocatingFunction = allocationFunction;
    allocatingType.objectSize = objectSize;
    allocatingType.isALargeObject = ProgramStatus::isALargeObject(objectSize);
    allocatingType.doingAllocation = true;
}

void AllocatingStatus::updateFreeingTypeBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress) {
    allocatingType.allocatingFunction = allocationFunction;
    allocatingType.objectAddress = objectAddress;
    allocatingType.doingAllocation = true;
}


void AllocatingStatus::startCountCountingEvents() {
    getPerfCounts(&countingDataBeforeRealFunction);
    cyclesBeforeRealFunction = rdtscp();
}

void AllocatingStatus::updateAllocatingStatusBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize) {
    updateAllocatingTypeBeforeRealFunction(allocationFunction, objectSize);
    startCountCountingEvents();
}

void AllocatingStatus::updateFreeingStatusBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress) {
    updateFreeingTypeBeforeRealFunction(allocationFunction, objectAddress);
    AllocatingStatus::updateMemoryStatusBeforeFree();
    if(allocationFunction == FREE) {
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
    if(cyclesInRealFunction > ABNORMAL_VALUE_FOR_COUNTING_EVENTS) {
        cyclesInRealFunction = 0;
    }
    if(countingDataInRealFunction.faults > ABNORMAL_VALUE_FOR_COUNTING_EVENTS) {
        countingDataInRealFunction.faults = 0;
    }
    if(countingDataInRealFunction.cache_misses > ABNORMAL_VALUE_FOR_COUNTING_EVENTS) {
        countingDataInRealFunction.cache_misses = 0;
    }
    if(countingDataInRealFunction.tlb_read_misses > ABNORMAL_VALUE_FOR_COUNTING_EVENTS) {
        countingDataInRealFunction.tlb_read_misses = 0;
    }
    if(countingDataInRealFunction.tlb_write_misses > ABNORMAL_VALUE_FOR_COUNTING_EVENTS) {
        countingDataInRealFunction.tlb_write_misses = 0;
    }
    if(countingDataInRealFunction.instructions > ABNORMAL_VALUE_FOR_COUNTING_EVENTS) {
        countingDataInRealFunction.instructions = 0;
    }
}

void AllocatingStatus::stopCountCountingEvents() {
    cyclesAfterRealFunction = rdtscp();
    getPerfCounts(&countingDataAfterRealFunction);
    calculateCountingDataInRealFunction();
    removeAbnormalCountingEventValues();
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
    if(allocatingType.objectSize == 4294967316) {
        fprintf(stderr, "free = %d\n", allocatingType.allocatingFunction == FREE);
        abort();
    }
    allocatingType.allocatingTypeGotFromShadowMemory.objectNewTouchedPageSize = ShadowMemory::updateObject(allocatingType.objectAddress, allocatingType.objectSize, true);
    MemoryUsage::subRealSizeFromMemoryUsage(allocatingType.objectSize);
}

void AllocatingStatus::setAllocationTypeForOutputData() {
    if(allocatingType.allocatingFunction == MALLOC) {
        if(allocatingType.isALargeObject) {
            allocationTypeForOutputData = LARGE_MALLOC;
        } else if(allocatingType.allocatingTypeGotFromMemoryWaste.isReusedObject) {
            allocationTypeForOutputData = SMALL_REUSED_MALLOC;
        } else {
            allocationTypeForOutputData = SMALL_NEW_MALLOC;
        }
    } else if(allocatingType.allocatingFunction == FREE) {
        if(allocatingType.isALargeObject) {
            allocationTypeForOutputData = LARGE_FREE;
        } else {
            allocationTypeForOutputData = SMALL_FREE;
        }
    } else if(allocatingType.allocatingFunction == CALLOC) {
        allocationTypeForOutputData = NORMAL_CALLOC;
    } else if(allocatingType.allocatingFunction == REALLOC) {
        allocationTypeForOutputData = NORMAL_REALLOC;
    } else if(allocatingType.allocatingFunction == POSIX_MEMALIGN) {
        allocationTypeForOutputData = NORMAL_POSIX_MEMALIGN;
    } else {
        allocationTypeForOutputData = NORMAL_MEMALIGN;
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
    cleanLockFunctionsInfoInAllocatingStatus();
    addUpSyscallsInfoToThreadLocalData();
    cleanSyscallsInfoInAllocatingStatus();
}


void AllocatingStatus::updateAllocatingInfoToThreadLocalData() {
    addUpOtherFunctionsInfoToThreadLocalData();
    setAllocationTypeForOutputData();
    addUpCountingEventsToThreadLocalData();
}

void AllocatingStatus::addUpCountingEventsToThreadLocalData() {
    ThreadLocalStatus::numOfFunctions[allocationTypeForOutputData]++;
    ThreadLocalStatus::cycles[allocationTypeForOutputData] += cyclesInRealFunction;
    ThreadLocalStatus::countingEvents[allocationTypeForOutputData].add(countingDataInRealFunction);
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

void AllocatingStatus::checkAndStartRecordingACriticalSection() {
    criticalSectionStatus.checkAndStartRecordingACriticalSection();
}

void AllocatingStatus::checkAndStopRecordingACriticalSection() {
    criticalSectionStatus.checkAndStopRecordingACriticalSection();
}