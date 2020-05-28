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

    struct OverviewLockDataInAllocatingStatus {
        unsigned int numOfLocks;
        unsigned int numOfCalls;
        unsigned int numOfCallsWithContentions;
        uint64_t cycles;

        void addANewLock() {
            numOfLocks++;
        }

        void addAContention() {
            numOfCallsWithContentions++;
        }

        void addCallAndCycles(unsigned int numOfCalls, uint64_t cycles) {
            this->numOfCalls += numOfCalls;
            this->cycles += cycles;
        }

        void cleanUp() {
            numOfLocks = 0;
            numOfCalls = 0;
            numOfCallsWithContentions = 0;
            cycles = 0;
        }
    };

    struct QueueOfDetailLockDataInAllocatingStatus {
        struct DetailLockDataInAllocatingStatus {
            DetailLockData * addressOfHashLockData;
            unsigned int numOfCalls;
            unsigned int numOfCallsWithContentions;
            uint64_t cycles;

            void writingIntoAddress() {
                addressOfHashLockData->numOfCalls[allocationTypeForOutputData] += numOfCalls;
                addressOfHashLockData->numOfCallsWithContentions[allocationTypeForOutputData] += numOfCallsWithContentions;
                addressOfHashLockData->cycles[allocationTypeForOutputData] += cycles;
            }
        } queue[1000];
        int queueTail = -1;

        void writingNewDataInTheQueue(void * addressOfHashLockData) {
            queue[++queueTail].addressOfHashLockData = addressOfHashLockData;
        }

        void addAContention() {
            queue[queueTail].numOfCallsWithContentions++;
        }

        void addCallAndCycles(unsigned int numOfCalls, uint64_t cycles) {
            queue[queueTail].numOfCalls += numOfCalls;
            queue[queueTail].cycles += cycles;
        }

        void cleanUpQueue() {
            queueTail = -1;
        }

        void writingIntoHashTable() {
            for(int index = 0; index <= queueTail; ++index) {
                queue[index].writingIntoAddress();
            }
        }
    };

    struct CriticalSectionStatusInAllocatingStatus {
        unsigned int numOfOwningLocks;
        uint64_t cyclesBeforeCriticalSection;
        uint64_t cyclesAfterCriticalSection;
        unsigned int numOfCriticalSections;
        uint64_t totalCyclesOfCriticalSections;

        void checkAndStartRecordingACriticalSection() {
            if(++numOfOwningLocks == 1) {
                cyclesBeforeRealFunction = rdtscp();
            }
        }

        void checkAndStopRecordingACriticalSection() {
            if(--numOfOwningLocks == 0) {
                cyclesAfterRealFunction = rdtscp();
                numOfCriticalSections++;
                totalCyclesOfCriticalSections += cyclesAfterCriticalSection - cyclesBeforeRealFunction;
            }
        }

        void cleanUp() {
            numOfCriticalSections = 0;
            totalCyclesOfCriticalSections = 0;
        }
    };
    static thread_local LockTypes nowRunningLockType;
    static thread_local QueueOfDetailLockDataInAllocatingStatus queueOfDetailLockData;
    static thread_local OverviewLockDataInAllocatingStatus overviewLockData[NUM_OF_LOCKTYPES];
    static thread_local CriticalSectionStatusInAllocatingStatus criticalSectionStatus;
    static thread_local SystemCallData systemCallData[NUM_OF_SYSTEMCALLTYPES];

    static void updateAllocatingTypeBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize);
    static void updateFreeingTypeBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress);

    static void startCountCountingEvents();
    static void updateAllocatingTypeAfterRealFunction(void * objectAddress);
    static void updateFreeingStatusAfterRealFunction();

    static void updateMemoryStatusAfterAllocation();
    static void updateMemoryStatusBeforeFree();
    static void addUpOverviewLockDataToThreadLocalData();
    static void addUpDetailLockDataToHashTable();
    static void addUpCriticalSectionDataToThreadLocalData();
    static void addUpLockFunctionsInfoToThreadLocalData();
    static void addUpSyscallsInfoToThreadLocalData();
    static void addUpOtherFunctionsInfoToThreadLocalData();
    static void addUpCountingEventsToThreadLocalData();

    static void calculateCountingDataInRealFunction();
    static void removeAbnormalCountingEventValues();
    static void stopCountCountingEvents();

    static void setAllocationTypeForOutputData();

    static void cleanOverviewLockDataInAllocatingStatus();
    static void cleanDetailLockDataInAllocatingStatus();
    static void cleanCriticalSectionDataInAllocatingStatus();
    static void cleanLockFunctionsInfoInAllocatingStatus();

public:
    static void updateAllocatingStatusBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize);
    static void updateFreeingStatusBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress);

    static void updateAllocatingStatusAfterRealFunction(void * objectAddress);
    static void updateFreeingTypeAfterRealFunction();

    static void updateAllocatingInfoToThreadLocalData();
    static bool outsideTrackedAllocation();
    static void addToSystemCallData(SystemCallTypes systemCallTypes, SystemCallData newSystemCallData);

    static void recordANewLock(LockTypes lockType);
    static void initForWritingOneLockData(LockTypes lockType, void * addressOfHashLockData);
    static void recordALockContention();
    static void recordLockCallAndCycles(unsigned int numOfCalls, uint64_t cycles);
    static void checkAndStartRecordingACriticalSection();
    static void checkAndStopRecordingACriticalSection();

};

#endif //MMPROF_ALLOCATIONSTATUS_H