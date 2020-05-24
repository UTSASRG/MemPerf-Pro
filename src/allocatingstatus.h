//
// Created by 86152 on 2020/5/23.
//

#ifndef MMPROF_ALLOCATIONSTATUS_H
#define MMPROF_ALLOCATIONSTATUS_H

#endif //MMPROF_ALLOCATIONSTATUS_H

typedef enum {
    MALLOC,
    CALLOC,
    FREE,
    REALLOC,
    POSIX_MEMALIGN,
    MEMALIGN
} AllocationFunction;

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
    bool doingAllocation;
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
    static thread_local uint64_t cyclesBeforeRealFunction;
    static thread_local uint64_t cyclesAfterRealFunction;
    static thread_local PerfReadInfo countingDataBeforeRealFunction;
    static thread_local PerfReadInfo countingDataAfterRealFunction;

    static void updateAllocatingTypeBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize);
    static void updateFreeingTypeBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress);

    static void startCountCountingEvents();
    static void updateAllocatingTypeAfterRealFunction(void * objectAddress);
    static void updateFreeingStatusAfterRealFunction();

    static void updateMemoryStatusAfterAllocation();
    static void updateMemoryStatusBeforeFree();
    static void addUpSystemCallsInfoToThreadLocalData();
    static void addUpCountingEventsToThreadLocalData();

    static void stopCountCountingEvents();


public:
    static void updateAllocatingStatusBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize);
    static void updateFreeingStatusBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress);

    static void updateAllocatingStatusAfterRealFunction(void * objectAddress);
    static void updateFreeingTypeAfterRealFunction();

    static void updateAllocatingInfoToThreadLocalData();

};
