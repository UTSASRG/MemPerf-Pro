#include "allocatingstatus.h"

thread_local bool AllocatingStatus::firstAllocation;
thread_local AllocatingType AllocatingStatus::allocatingType;
thread_local AllocationTypeForOutputData AllocatingStatus::allocationTypeForOutputData;
thread_local AllocationTypeForOutputData AllocatingStatus::allocationTypeForPrediction;
thread_local bool AllocatingStatus::sampledForCountingEvent;
thread_local uint64_t AllocatingStatus::cyclesBeforeRealFunction;
thread_local uint64_t AllocatingStatus::cyclesAfterRealFunction;
thread_local uint64_t AllocatingStatus::cyclesInRealFunction;
thread_local LockTypes AllocatingStatus::nowRunningLockType;
thread_local AllocatingStatus::QueueOfDetailLockDataInAllocatingStatus AllocatingStatus::queueOfDetailLockData;
thread_local AllocatingStatus::OverviewLockDataInAllocatingStatus AllocatingStatus::overviewLockData[NUM_OF_LOCKTYPES];
thread_local SystemCallData AllocatingStatus::systemCallData[NUM_OF_SYSTEMCALLTYPES];

#ifdef COUNTING
thread_local PerfReadInfo AllocatingStatus::countingDataBeforeRealFunction;
thread_local PerfReadInfo AllocatingStatus::countingDataAfterRealFunction;
thread_local PerfReadInfo AllocatingStatus::countingDataInRealFunction;
#endif

bool AllocatingStatus::isFirstFunction() {
    if(firstAllocation == false) {
        firstAllocation = true;
        return true;
    }
    return false;
}


void AllocatingStatus::QueueOfDetailLockDataInAllocatingStatus::writingNewDataInTheQueue(DetailLockData * addressOfHashLockData) {
//    if(queueTail == LENGTH_OF_QUEUE-1) {
//        writingIntoHashTable();
//        cleanUpQueue();
//    }
    if(queueTail < LENGTH_OF_QUEUE - 1) {
        queueTail++;
        queue[queueTail].addressOfHashLockData = addressOfHashLockData;
    }
}

void AllocatingStatus::QueueOfDetailLockDataInAllocatingStatus::cleanUpQueue() {
    memset(queue, 0, sizeof(QueueOfDetailLockDataInAllocatingStatus::DetailLockDataInAllocatingStatus) * (queueTail + 1));
    queueTail = -1;
}

void AllocatingStatus::updateAllocatingTypeBeforeRealFunction(AllocationFunction allocationFunction, unsigned int objectSize) {
    allocatingType.allocatingFunction = allocationFunction;
    allocatingType.objectSize = objectSize;
    allocatingType.doingAllocation = true;
}

void AllocatingStatus::updateFreeingTypeBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress) {
    allocatingType.allocatingFunction = allocationFunction;
    allocatingType.objectAddress = objectAddress;
    allocatingType.doingAllocation = true;
}

void AllocatingStatus::updateAllocatingStatusBeforeRealFunction(AllocationFunction allocationFunction, unsigned int objectSize) {

    updateAllocatingTypeBeforeRealFunction(allocationFunction, objectSize);
//    numFunc++;
#ifdef OPEN_SAMPLING_FOR_ALLOCS
    sampledForCountingEvent = ThreadLocalStatus::randomProcessForCountingEvent();
#else
    sampledForCountingEvent = true;
#endif

#ifdef COUNTING
    if(sampledForCountingEvent) {
        startCountCountingEvents();
    }
#endif

    cyclesBeforeRealFunction = rdtscp();
}

void AllocatingStatus::updateFreeingStatusBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress) {

    updateFreeingTypeBeforeRealFunction(allocationFunction, objectAddress);
    updateMemoryStatusBeforeFree();
//    numFunc++;
    if(allocationFunction == FREE) {
#ifdef OPEN_SAMPLING_FOR_ALLOCS
        sampledForCountingEvent = ThreadLocalStatus::randomProcessForCountingEvent();
#else
        sampledForCountingEvent = true;
#endif

#ifdef COUNTING
        if(sampledForCountingEvent) {
            startCountCountingEvents();
        }
#endif

        cyclesBeforeRealFunction = rdtscp();
    }
}

void AllocatingStatus::updateAllocatingStatusAfterRealFunction(void * objectAddress) {

    cyclesAfterRealFunction = rdtscp();

#ifdef COUNTING
    if(sampledForCountingEvent) {
        stopCountCountingEvents();
    }
#endif

    cyclesInRealFunction = cyclesAfterRealFunction - cyclesBeforeRealFunction;
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

#ifdef COUNTING
    if(sampledForCountingEvent) {
        stopCountCountingEvents();
    }
#endif

    cyclesInRealFunction = cyclesAfterRealFunction - cyclesBeforeRealFunction;
    if(cyclesInRealFunction > ABNORMAL_VALUE) {
        cyclesInRealFunction = 0;
    }

    allocatingType.doingAllocation = false;
}

void AllocatingStatus::updateMemoryStatusAfterAllocation() {

#ifdef OPEN_BACKTRACE
    uint8_t callsiteKey = Backtrace::doABackTrace(allocatingType.objectSize);
#endif

#ifdef MEMORY_WASTE
    allocatingType.isReuse = MemoryWaste::allocUpdate(allocatingType.objectSize, allocatingType.objectAddress, callsiteKey);
#else
    allocatingType.isReuse = ObjTable::allocUpdate(allocatingType.objectSize, allocatingType.objectAddress);
#endif

#ifdef MEMORY
    unsigned int touchedPageSize = ShadowMemory::mallocUpdateObject(allocatingType.objectAddress, allocatingType.objectSize);
    MemoryUsage::addToMemoryUsage(allocatingType.objectSize, touchedPageSize);
#else
    ShadowMemory::mallocUpdateObject(allocatingType.objectAddress, allocatingType.objectSize);
#endif

}

void AllocatingStatus::updateMemoryStatusBeforeFree() {
#ifdef MEMORY_WASTE
    ObjectStatus * objStat = MemoryWaste::freeUpdate(allocatingType.objectAddress);
    if(objStat) {
        allocatingType.objectSize = objStat->sizeClassSizeAndIndex.size;
        if(allocatingType.objectSize) {
            ShadowMemory::freeUpdateObject(allocatingType.objectAddress, *objStat);
            MemoryUsage::subRealSizeFromMemoryUsage(allocatingType.objectSize);
        }
    }
#else
    ObjStat * objStat = ObjTable::freeUpdate(allocatingType.objectAddress);
    if(objStat) {
        allocatingType.objectSize = objStat->size;
        if(allocatingType.objectSize) {
            ShadowMemory::freeUpdateObject(allocatingType.objectAddress, *objStat);
        }
    }
#endif

}

void AllocatingStatus::setAllocationTypeForOutputData() {
    if(allocatingType.allocatingFunction == MALLOC) {
        allocationTypeForOutputData = (AllocationTypeForOutputData)(ThreadLocalStatus::isCurrentlyParallelThread() * 5 + allocatingType.objectSizeType * 2
                + (allocatingType.objectSizeType != LARGE && allocatingType.isReuse));

    } else if(allocatingType.allocatingFunction == FREE) {
        allocationTypeForOutputData = (AllocationTypeForOutputData)(10 + ThreadLocalStatus::isCurrentlyParallelThread() * 3 + allocatingType.objectSizeType);

    } else if(allocatingType.allocatingFunction == CALLOC) {
        allocationTypeForOutputData = (AllocationTypeForOutputData)(16 + ThreadLocalStatus::isCurrentlyParallelThread());

    } else if(allocatingType.allocatingFunction == REALLOC) {
        allocationTypeForOutputData = (AllocationTypeForOutputData)(18 + ThreadLocalStatus::isCurrentlyParallelThread());

    } else if(allocatingType.allocatingFunction == POSIX_MEMALIGN) {
        allocationTypeForOutputData = (AllocationTypeForOutputData)(20 + ThreadLocalStatus::isCurrentlyParallelThread());

    } else {
        allocationTypeForOutputData = (AllocationTypeForOutputData)(22 + ThreadLocalStatus::isCurrentlyParallelThread());
    }
}

#ifdef PREDICTION
void AllocatingStatus::setAllocationTypeForPrediction() {
    if(allocatingType.objectSizeType == allocatingType.objectSizeTypeForPrediction) {
        allocationTypeForPrediction = allocationTypeForOutputData;
    } else if(allocatingType.objectSizeType > allocatingType.objectSizeTypeForPrediction) {
        if(allocatingType.allocatingFunction == MALLOC) {
            allocationTypeForPrediction = (AllocationTypeForOutputData)(
                    (int)allocationTypeForOutputData - (allocatingType.objectSizeType - allocatingType.objectSizeTypeForPrediction) * 2);

        } else if(allocatingType.allocatingFunction == FREE) {
            allocationTypeForPrediction = (AllocationTypeForOutputData)(
                    (int)allocationTypeForOutputData - (allocatingType.objectSizeType - allocatingType.objectSizeTypeForPrediction));
        }
    } else {
        if(allocatingType.allocatingFunction == MALLOC) {
            allocationTypeForPrediction = (AllocationTypeForOutputData)(
                    (int)allocationTypeForOutputData + (allocatingType.objectSizeTypeForPrediction - allocatingType.objectSizeType) * 2);

        } else if(allocatingType.allocatingFunction == FREE) {
            allocationTypeForPrediction = (AllocationTypeForOutputData)(
                    (int)allocationTypeForOutputData + (allocatingType.objectSizeTypeForPrediction - allocatingType.objectSizeType));
        }
    }

}

void AllocatingStatus::setAllocationTypeForPredictionRaw() {
        if(allocatingType.allocatingFunction == MALLOC) {
            allocationTypeForPrediction = (AllocationTypeForOutputData)(ThreadLocalStatus::isCurrentlyParallelThread() * 5 + allocatingType.objectSizeTypeForPrediction * 2
                                                                        + (allocatingType.objectSizeType != LARGE && allocatingType.isReuse));

        } else if(allocatingType.allocatingFunction == FREE) {
            allocationTypeForPrediction = (AllocationTypeForOutputData)(10 + ThreadLocalStatus::isCurrentlyParallelThread() * 3 + allocatingType.objectSizeTypeForPrediction);

        } else if(allocatingType.allocatingFunction == CALLOC) {
            allocationTypeForPrediction = (AllocationTypeForOutputData)(16 + ThreadLocalStatus::isCurrentlyParallelThread());

        } else if(allocatingType.allocatingFunction == REALLOC) {
            allocationTypeForPrediction = (AllocationTypeForOutputData)(18 + ThreadLocalStatus::isCurrentlyParallelThread());

        } else if(allocatingType.allocatingFunction == POSIX_MEMALIGN) {
            allocationTypeForPrediction = (AllocationTypeForOutputData)(20 + ThreadLocalStatus::isCurrentlyParallelThread());

        } else {
            allocationTypeForPrediction = (AllocationTypeForOutputData)(22 + ThreadLocalStatus::isCurrentlyParallelThread());
        }
}
#endif

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

//void AllocatingStatus::addUpCriticalSectionDataToThreadLocalData() {
//    ThreadLocalStatus::criticalSectionStatus[allocationTypeForOutputData].numOfCriticalSections += criticalSectionStatus.numOfCriticalSections;
//    ThreadLocalStatus::criticalSectionStatus[allocationTypeForOutputData].totalCyclesOfCriticalSections += criticalSectionStatus.totalCyclesOfCriticalSections;
//}

void AllocatingStatus::addUpLockFunctionsInfoToThreadLocalData() {
    addUpOverviewLockDataToThreadLocalData();
    addUpDetailLockDataToHashTable();
//    addUpCriticalSectionDataToThreadLocalData();
}

void AllocatingStatus::cleanLockFunctionsInfoInAllocatingStatus() {
    memset(overviewLockData, 0, sizeof(OverviewLockDataInAllocatingStatus) * NUM_OF_LOCKTYPES);
    queueOfDetailLockData.cleanUpQueue();
//    criticalSectionStatus.cleanUp();
}

void AllocatingStatus::addUpSyscallsInfoToThreadLocalData() {
    for(int syscallType = 0; syscallType < NUM_OF_SYSTEMCALLTYPES; ++syscallType) {
        ThreadLocalStatus::systemCallData[syscallType][allocationTypeForOutputData].add(systemCallData[syscallType]);
    }
}

void AllocatingStatus::addUpOtherFunctionsInfoToThreadLocalData() {
    addUpLockFunctionsInfoToThreadLocalData();
    addUpSyscallsInfoToThreadLocalData();
}


void AllocatingStatus::updateAllocatingInfoToThreadLocalData() {
    if(sampledForCountingEvent) {
        allocatingType.objectSizeType = ProgramStatus::getObjectSizeType(allocatingType.objectSize);
        setAllocationTypeForOutputData();
//    ThreadLocalStatus::numOfFunctions[allocationTypeForOutputData]++;
        addUpOtherFunctionsInfoToThreadLocalData();
        addUpCountingEventsToThreadLocalData();
        cleanLockFunctionsInfoInAllocatingStatus();
        memset(systemCallData, 0, sizeof(SystemCallData) * NUM_OF_SYSTEMCALLTYPES);
    }
}

#ifdef PREDICTION
void AllocatingStatus::updateAllocatingInfoToPredictor() {
        allocatingType.objectSizeTypeForPrediction = Predictor::getObjectSizeTypeForPrediction(allocatingType.objectSize);
        if(sampledForCountingEvent) {
            AllocatingStatus::setAllocationTypeForPrediction();
        } else {
            AllocatingStatus::setAllocationTypeForPredictionRaw();
        }
    if(Predictor::replacedFunctionCycles[allocationTypeForPrediction]) {
        Predictor::numOfFunctions[allocationTypeForPrediction]++;
        Predictor::totalFunctionCycles += cyclesInRealFunction;
    }
}
#endif


void AllocatingStatus::addUpCountingEventsToThreadLocalData() {
    ThreadLocalStatus::numOfSampledCountingFunctions[allocationTypeForOutputData]++;
    ThreadLocalStatus::cycles[allocationTypeForOutputData] += cyclesInRealFunction;
#ifdef COUNTING
    ThreadLocalStatus::countingEvents[allocationTypeForOutputData].add(countingDataInRealFunction);
#endif
}

bool AllocatingStatus::outsideTrackedAllocation() {
    return !allocatingType.doingAllocation;
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

#ifdef COUNTING

void AllocatingStatus::startCountCountingEvents() {
    getPerfCounts(&countingDataBeforeRealFunction);
}

void AllocatingStatus::stopCountCountingEvents() {
    getPerfCounts(&countingDataAfterRealFunction);
    calculateCountingDataInRealFunction();
    removeAbnormalCountingEventValues();
}

void AllocatingStatus::calculateCountingDataInRealFunction() {
    countingDataInRealFunction.faults = countingDataAfterRealFunction.faults - countingDataBeforeRealFunction.faults;
    countingDataInRealFunction.l1cache_load = countingDataAfterRealFunction.l1cache_load - countingDataBeforeRealFunction.l1cache_load;
    countingDataInRealFunction.l1cache_load_miss = countingDataAfterRealFunction.l1cache_load_miss - countingDataBeforeRealFunction.l1cache_load_miss;
//    countingDataInRealFunction.llc_load = countingDataAfterRealFunction.llc_load - countingDataBeforeRealFunction.llc_load;
//    countingDataInRealFunction.llc_load_miss = countingDataAfterRealFunction.llc_load_miss - countingDataBeforeRealFunction.llc_load_miss;
    countingDataInRealFunction.instructions = countingDataAfterRealFunction.instructions - countingDataBeforeRealFunction.instructions;
}

void AllocatingStatus::removeAbnormalCountingEventValues() {
    if(cyclesInRealFunction > ABNORMAL_VALUE) {
        cyclesInRealFunction = 0;
    }
    if(countingDataInRealFunction.faults > ABNORMAL_VALUE) {
        countingDataInRealFunction.faults = 0;
    }
    if(countingDataInRealFunction.l1cache_load > ABNORMAL_VALUE) {
        countingDataInRealFunction.l1cache_load = 0;
    }
    if(countingDataInRealFunction.l1cache_load_miss > ABNORMAL_VALUE) {
        countingDataInRealFunction.l1cache_load_miss = 0;
    }
//    if(countingDataInRealFunction.llc_load > ABNORMAL_VALUE) {
//        countingDataInRealFunction.llc_load = 0;
//    }
//    if(countingDataInRealFunction.llc_load_miss > ABNORMAL_VALUE) {
//        countingDataInRealFunction.llc_load_miss = 0;
//    }
    if(countingDataInRealFunction.instructions > ABNORMAL_VALUE) {
        countingDataInRealFunction.instructions = 0;
    }
}
#endif
