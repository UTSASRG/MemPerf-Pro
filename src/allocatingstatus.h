#ifndef MMPROF_ALLOCATIONSTATUS_H
#define MMPROF_ALLOCATIONSTATUS_H

#include "shadowmemory.hh"
#include "memwaste.h"
#include "memoryusage.h"
#include "threadlocalstatus.h"
#include "structs.h"


class AllocatingStatus {

private:
    static thread_local AllocatingType allocatingType;
    static thread_local AllocationTypeForOutputData allocationTypeForOutputData;
    static thread_local bool sampledForCountingEvent;
    static thread_local uint64_t cyclesBeforeRealFunction;
    static thread_local uint64_t cyclesAfterRealFunction;
    static thread_local uint64_t cyclesInRealFunction;
    static thread_local PerfReadInfo countingDataBeforeRealFunction;
    static thread_local PerfReadInfo countingDataAfterRealFunction;
    static thread_local PerfReadInfo countingDataInRealFunction;
    static spinlock debugLock;

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

        void debugPrint() {
            fprintf(stderr, "numOfLocks = %u, numOfCalls = %u, numOfCallsWithContentions = %u, cycles = %lu\n",
                    numOfLocks, numOfCalls, numOfCallsWithContentions, cycles);
        }
    };

#define LENGTH_OF_QUEUE 500

    struct QueueOfDetailLockDataInAllocatingStatus {
        struct DetailLockDataInAllocatingStatus {
            DetailLockData * addressOfHashLockData;
            unsigned int numOfCalls;
            unsigned int numOfCallsWithContentions;
            uint64_t cycles;

            uint64_t lockTimeStamp;
            uint64_t unlockTimeStamp;
            pthread_mutex_t * debugMutexAddress;

            void writingIntoAddress() {
                addressOfHashLockData->numOfCalls[allocationTypeForOutputData] += numOfCalls;
                addressOfHashLockData->numOfCallsWithContentions[allocationTypeForOutputData] += numOfCallsWithContentions;
                addressOfHashLockData->cycles[allocationTypeForOutputData] += cycles;
            }
        } queue[LENGTH_OF_QUEUE];
        int queueTail = -1;

        void writingNewDataInTheQueue(DetailLockData * addressOfHashLockData);

        void addAContention() {
            queue[queueTail].numOfCallsWithContentions++;
        }

        void addCallAndCycles(unsigned int numOfCalls, uint64_t cycles) {
            queue[queueTail].numOfCalls += numOfCalls;
            queue[queueTail].cycles += cycles;
        }

        void cleanUpQueue();

        void writingIntoHashTable() {
            for(int index = 0; index <= queueTail; ++index) {
                queue[index].writingIntoAddress();
            }
        }

        void debugAddMutexAddress(uint64_t lockTimeStamp, pthread_mutex_t * mutex) {
            queue[queueTail].lockTimeStamp = lockTimeStamp;
            queue[queueTail].debugMutexAddress = mutex;
        }

        void debugAddUnlockTimeStamp(uint64_t unlockTimeStamp, pthread_mutex_t * mutex) {
            for(int index = 0; index <= queueTail; ++index) {
                if(queue[index].debugMutexAddress == mutex && queue[index].unlockTimeStamp == 0) {
                    queue[index].unlockTimeStamp = unlockTimeStamp;
                    return;
                }
            }
            fprintf(stderr, "didn't find mutex in the queue: %p\n", mutex);
            debugPrint();
            abort();
        }

        bool debugMutexAddressInTheQueue(pthread_mutex_t * mutex) {
            for(int index = 0; index <= queueTail; ++index) {
                if(queue[index].debugMutexAddress == mutex) {
                    return true;
                }
            }
            return false;
        }

        void debugPrint() {
            for(int index = 0; index <= queueTail; ++index) {
                fprintf(stderr, "mutex = %p, contention = %u, cycles = %lu\n",
                        queue[index].debugMutexAddress, queue[index].numOfCallsWithContentions, queue[index].cycles);
            }
        }

        void debugPrint(unsigned int threadIndex) {
            for(int index = 0; index <= queueTail; ++index) {
                fprintf(stderr, "%lu, %u, %p, %u, %lu\n",
                        queue[index].lockTimeStamp, threadIndex, queue[index].debugMutexAddress, queue[index].numOfCallsWithContentions, queue[index].cycles);
                fprintf(stderr, "%lu, %u, %p, %u, %lu\n",
                        queue[index].unlockTimeStamp, threadIndex, queue[index].debugMutexAddress, queue[index].numOfCallsWithContentions, queue[index].cycles);
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
                cyclesBeforeCriticalSection = rdtscp();
            }
        }

        void checkAndStopRecordingACriticalSection() {
            if(--numOfOwningLocks == 0) {
                cyclesAfterCriticalSection = rdtscp();
                numOfCriticalSections++;
                totalCyclesOfCriticalSections += cyclesAfterCriticalSection - cyclesBeforeCriticalSection;
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
    static void updateAllocatingTypeAfterRealFunction(void * objectAddress);
    static void updateFreeingTypeBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress);

    static void updateFreeingTypeAfterRealFunction();
    static void startCountCountingEvents();

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
    static void cleanSyscallsInfoInAllocatingStatus();

public:
    static void updateAllocatingStatusBeforeRealFunction(AllocationFunction allocationFunction, size_t objectSize);
    static void updateFreeingStatusBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress);

    static void updateAllocatingStatusAfterRealFunction(void * objectAddress);
    static void updateFreeingStatusAfterRealFunction();

    static void updateAllocatingInfoToThreadLocalData();
    static bool outsideTrackedAllocation();
    static void addOneSyscallToSyscallData(SystemCallTypes systemCallTypes, uint64_t cycles);

    static void recordANewLock(LockTypes lockType);
    static void initForWritingOneLockData(LockTypes lockType, DetailLockData* addressOfHashLockData);
    static void recordALockContention();
    static void recordLockCallAndCycles(unsigned int numOfCalls, uint64_t cycles);
    static void debugRecordMutexAddress(uint64_t lockTimeStamp, pthread_mutex_t * mutex);
    static void debugRecordUnlockTimeStamp(uint64_t unlockTimeStamp, pthread_mutex_t * mutex);
    static bool debugMutexAddressInTheQueue(pthread_mutex_t * mutex);
    static void checkAndStartRecordingACriticalSection();
    static void checkAndStopRecordingACriticalSection();

    static void debugPrint();
    static size_t debugReturnSize();

};

#endif //MMPROF_ALLOCATIONSTATUS_H