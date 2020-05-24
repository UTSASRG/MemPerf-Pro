//
// Created by 86152 on 2020/5/23.
//

#include "allocatingstatus.h"
#include "shadowmemory.hh"
#include "memwaste.h"
#include "programstatus.h"

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


void AllocatingStatus::stopCountCountingEvents() {
    cyclesAfterRealFunction = rdtscp();
    getPerfCounts(&countingDataAfterRealFunction);
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
    incrementTotalMemoryUsage();
}

void AllocatingStatus::updateMemoryStatusBeforeFree() {
    allocatingType.switchFreeingTypeGotFromMemoryWaste(MemoryWaste::freeUpdate(allocatingType.objectAddress));
    allocatingType.allocatingTypeGotFromShadowMemory = ShadowMemory::updateObject(allocatingType.objectAddress, allocatingType.objectSize, true);
    decrementMemoryUsage();
}

void AllocatingStatus::updateAllocatingInfoToThreadLocalData() {
    addUpSystemCallsInfoToThreadLocalData();
    addUpCountingEventsToThreadLocalData();
}