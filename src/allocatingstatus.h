//
// Created by 86152 on 2020/5/23.
//

#ifndef MMPROF_ALLOCATIONSTATUS_H
#define MMPROF_ALLOCATIONSTATUS_H

#include "recordscale.hh"

typedef enum {
    MALLOC,
    FREE,
    CALLOC,
    REALLOC,
    POSIX_MEMALIGN,
    MEMALIGN
    NUM_OF_ALLOCATIONFUNCTION
} AllocationFunction;

typedef enum {
    SMALL_NEW_MALLOC,
    SMALL_REUSED_MALLOC,
    LARGE_MALLOC,
    SMALL_FREE,
    LARGE_FREE,
    NORMAL_CALLOC,
    NORMAL_REALLOC,
    NORMAL_POSIX_MEMALIGN,
    NORMAL_MEMALIGN,
    NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA
} AllocationTypeForOutputData;

struct AllocatingTypeGotFromMemoryWaste {
    bool isReusedObject;
    size_t objectClassSize;
}
struct AllocatingTypeWithSizeGotFromMemoryWaste {
    size_t objectSize;
    AllocatingTypeGotFromMemoryWaste allocatingTypeGotByMemoryWaste;
}
struct AllocatingTypeGotFromShadowMemory {
    size_t objectNewTouchedPageSize;
}
struct AllocatingType {
    AllocationFunction allocatingFunction;
    size_t objectSize;
    bool isALargeObject;
    bool doingAllocation = false;
    void * objectAddress;
    AllocatingTypeGotFromMemoryWaste allocatingTypeGotFromMemoryWaste;
    AllocatingTypeGotFromShadowMemory allocatingTypeGotFromShadowMemory;

    void switchFreeingTypeGotFromMemoryWaste(AllocatingTypeWithSizeGotFromMemoryWaste allocatingTypeWithSizeGotFromMemoryWaste) {
        objectSize = allocatingTypeWithSizeGotFromMemoryWaste.objectSize;
        isALargeObject = ProgramStatus::isALargeObject(objectSize);
        allocatingTypeGotFromMemoryWaste = allocatingTypeWithSizeGotFromMemoryWaste.allocatingTypeGotByMemoryWaste;
    };
};



class AllocatingStatus {

private:
    static thread_local AllocatingType allocatingType;
    static thread_local AllocationTypeForOutputData allocationTypeForOutputData;
    static thread_local uint64_t cyclesBeforeRealFunction;
    static thread_local uint64_t cyclesAfterRealFunction;
    static thread_local uint64_t cyclesInRealFunction;
    static thread_local PerfReadInfo countingDataBeforeRealFunction;
    static thread_local PerfReadInfo countingDataAfterRealFunction;
    static thread_local PerfReadInfo countingDataInRealFunction;

    static thread_local SystemCallData systemCallData[NUM_OF_SYSTEMCALLTYPES];

    static void updateAllocatingTypeBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize);
    static void updateFreeingTypeBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress);

    static void startCountCountingEvents();
    static void updateAllocatingTypeAfterRealFunction(void * objectAddress);
    static void updateFreeingStatusAfterRealFunction();

    static void updateMemoryStatusAfterAllocation();
    static void updateMemoryStatusBeforeFree();
    static void addUpSystemCallsInfoToThreadLocalData();
    static void addUpCountingEventsToThreadLocalData();

    static void calculateCountingDataInRealFunction();
    static void removeAbnormalCountingEventValues();
    static void stopCountCountingEvents();

    static void setAllocationTypeForOutputData();


public:
    static void updateAllocatingStatusBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize);
    static void updateFreeingStatusBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress);

    static void updateAllocatingStatusAfterRealFunction(void * objectAddress);
    static void updateFreeingTypeAfterRealFunction();

    static void updateAllocatingInfoToThreadLocalData();
    static bool outsideTrackedAllocation();
    static void addToSystemCallData(SystemCallTypes systemCallTypes, SystemCallData newSystemCallData);

};

#endif //MMPROF_ALLOCATIONSTATUS_H