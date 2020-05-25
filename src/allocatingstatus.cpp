//
// Created by 86152 on 2020/5/23.
//

#include "allocatingstatus.h"
#include "shadowmemory.hh"
#include "memwaste.h"
#include "programstatus.h"
#include "memoryusage.h"
#include "threadlocalstatus.h"

thread_local AllocatingType AllocatingStatus::allocatingType;

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
    allocatingType.allocatingTypeGotFromShadowMemory = ShadowMemory::updateObject(allocatingType.objectAddress, allocatingType.objectSize, false);
    MemoryUsage::addToMemoryUsage(allocatingType.objectSize, allocatingType.allocatingTypeGotFromShadowMemory.objectNewTouchedPageSize);
}

void AllocatingStatus::updateMemoryStatusBeforeFree() {
    allocatingType.switchFreeingTypeGotFromMemoryWaste(MemoryWaste::freeUpdate(allocatingType.objectAddress));
    allocatingType.allocatingTypeGotFromShadowMemory = ShadowMemory::updateObject(allocatingType.objectAddress, allocatingType.objectSize, true);
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

void AllocatingStatus::updateAllocatingInfoToThreadLocalData() {
    setAllocationTypeForOutputData();
    addUpSystemCallsInfoToThreadLocalData();
    addUpCountingEventsToThreadLocalData();
}

void AllocatingStatus::addUpCountingEventsToThreadLocalData() {
    ThreadLocalStatus::numOfFunctions[allocationTypeForOutputData]++;
    ThreadLocalStatus::cycles[allocationTypeForOutputData] += cyclesInRealFunction;
    ThreadLocalStatus::countingEvents[allocationTypeForOutputData].faults += countingDataInRealFunction.faults;
    ThreadLocalStatus::countingEvents[allocationTypeForOutputData].instructions += countingDataInRealFunction.instructions;
    ThreadLocalStatus::countingEvents[allocationTypeForOutputData].cache_misses += countingDataInRealFunction.cache_misses;
    ThreadLocalStatus::countingEvents[allocationTypeForOutputData].tlb_read_misses += countingDataInRealFunction.tlb_read_misses;
    ThreadLocalStatus::countingEvents[allocationTypeForOutputData].tlb_write_misses += countingDataInRealFunction.tlb_write_misses;
}

bool AllocatingStatus::outsideTrackedAllocation() {
    return ! allocatingType.doingAllocation;
}

void AllocatingStatus::addToSystemCallData(SystemCallTypes systemCallTypes, SystemCallData newSystemCallData) {
    systemCallData[systemCallTypes].add(newSystemCallData);
}