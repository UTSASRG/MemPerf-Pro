#ifndef MMPROF_ALLOCATIONSTATUS_H
#define MMPROF_ALLOCATIONSTATUS_H


#include <assert.h>
#include "shadowmemory.hh"
#include "objTable.h"
#include "threadlocalstatus.h"
#include "structs.h"
#include "predictor.h"
#include "memoryusage.h"
#include "backtrace.h"
#include "memwaste.h"


class AllocatingStatus {

private:
    static thread_local AllocationTypeForOutputData allocationTypeForOutputData;
    static thread_local AllocationTypeForOutputData allocationTypeForPrediction;
    static thread_local uint64_t cyclesBeforeRealFunction;
    static thread_local uint64_t cyclesAfterRealFunction;
    static thread_local uint64_t cyclesInRealFunction;

#ifdef OPEN_DEBUG
    static spinlock debugLock;
#endif

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

#ifdef OPEN_DEBUG
        void debugPrint() {
            fprintf(stderr, "numOfLocks = %u, numOfCalls = %u, numOfCallsWithContentions = %u, cycles = %lu\n",
                    numOfLocks, numOfCalls, numOfCallsWithContentions, cycles);
        }
#endif
    };

#define LENGTH_OF_QUEUE 10

    struct QueueOfDetailLockDataInAllocatingStatus {

        short queueTail = -1;
        struct DetailLockDataInAllocatingStatus {
            unsigned int numOfCalls;
            unsigned int numOfCallsWithContentions;
            uint64_t cycles;
#ifdef OPEN_DEBUG
            uint64_t lockTimeStamp;
            uint64_t unlockTimeStamp;
            pthread_mutex_t * debugMutexAddress;
#endif
            DetailLockData * addressOfHashLockData;

            void writingIntoAddress() {
                addressOfHashLockData->numOfCalls[allocationTypeForOutputData] += numOfCalls;
                addressOfHashLockData->numOfCallsWithContentions[allocationTypeForOutputData] += numOfCallsWithContentions;
                addressOfHashLockData->cycles[allocationTypeForOutputData] += cycles;
            }
        } queue[LENGTH_OF_QUEUE];

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
            for(short index = 0; index <= queueTail; ++index) {
                queue[index].writingIntoAddress();
            }
        }
#ifdef OPEN_DEBUG
        void debugAddMutexAddress(uint64_t lockTimeStamp, pthread_mutex_t * mutex) {
            queue[queueTail].lockTimeStamp = lockTimeStamp;
            queue[queueTail].debugMutexAddress = mutex;
        }

        void debugAddUnlockTimeStamp(uint64_t unlockTimeStamp, pthread_mutex_t * mutex) {
            for(short index = 0; index <= queueTail; ++index) {
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
            for(short index = 0; index <= queueTail; ++index) {
                if(queue[index].debugMutexAddress == mutex) {
                    return true;
                }
            }
            return false;
        }

        void debugPrint() {
            for(short index = 0; index <= queueTail; ++index) {
                fprintf(stderr, "mutex = %p, contention = %u, cycles = %lu\n",
                        queue[index].debugMutexAddress, queue[index].numOfCallsWithContentions, queue[index].cycles);
            }
        }

        void debugPrint(unsigned int threadIndex) {
            for(short index = 0; index <= queueTail; ++index) {
                fprintf(stderr, "%lu, %u, %p, %u, %lu\n",
                        queue[index].lockTimeStamp, threadIndex, queue[index].debugMutexAddress, queue[index].numOfCallsWithContentions, queue[index].cycles);
                fprintf(stderr, "%lu, %u, %p, %u, %lu\n",
                        queue[index].unlockTimeStamp, threadIndex, queue[index].debugMutexAddress, queue[index].numOfCallsWithContentions, queue[index].cycles);
            }
        }
#endif
    };

    static thread_local LockTypes nowRunningLockType;
    static thread_local QueueOfDetailLockDataInAllocatingStatus queueOfDetailLockData;
    static thread_local OverviewLockDataInAllocatingStatus overviewLockData[NUM_OF_LOCKTYPES];
//    static thread_local CriticalSectionStatusInAllocatingStatus criticalSectionStatus;
    static thread_local SystemCallData systemCallData[NUM_OF_SYSTEMCALLTYPES];

#ifdef COUNTING
    static thread_local PerfReadInfo countingDataBeforeRealFunction;
    static thread_local PerfReadInfo countingDataAfterRealFunction;
    static thread_local PerfReadInfo countingDataInRealFunction;
#endif

    static void updateAllocatingTypeBeforeRealFunction(AllocationFunction allocationFunction, unsigned int objectSize);
    static void updateFreeingTypeBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress);

    static void updateMemoryStatusAfterAllocation();
    static void updateMemoryStatusBeforeFree();
    static void addUpOverviewLockDataToThreadLocalData();
    static void addUpDetailLockDataToHashTable();
    static void addUpCriticalSectionDataToThreadLocalData();
    static void addUpLockFunctionsInfoToThreadLocalData();
    static void addUpSyscallsInfoToThreadLocalData();
    static void addUpOtherFunctionsInfoToThreadLocalData();
    static void addUpCountingEventsToThreadLocalData();

    static void setAllocationTypeForOutputData();
    static void setAllocationTypeForPrediction();
    static void setAllocationTypeForPredictionRaw();

    static void cleanLockFunctionsInfoInAllocatingStatus();

public:

//    static thread_local uint8_t numFunc;
    static thread_local AllocatingType allocatingType;

    static thread_local bool firstAllocation;
    static thread_local bool sampledForCountingEvent;

    static bool isFirstFunction();

    static void updateAllocatingStatusBeforeRealFunction(AllocationFunction allocationFunction, unsigned int objectSize);
    static void updateFreeingStatusBeforeRealFunction(AllocationFunction allocationFunction, void * objectAddress);

    static void updateAllocatingStatusAfterRealFunction(void * objectAddress);
    static void updateFreeingStatusAfterRealFunction();

    static void updateAllocatingInfoToThreadLocalData();
    static void updateAllocatingInfoToPredictor();
    static bool outsideTrackedAllocation();
    static void addOneSyscallToSyscallData(SystemCallTypes systemCallTypes, uint64_t cycles);

    static void recordANewLock(LockTypes lockType);
    static void initForWritingOneLockData(LockTypes lockType, DetailLockData* addressOfHashLockData);
    static void recordALockContention();
    static void recordLockCallAndCycles(unsigned int numOfCalls, uint64_t cycles);
#ifdef OPEN_DEBUG
    static void debugRecordMutexAddress(uint64_t lockTimeStamp, pthread_mutex_t * mutex);
    static void debugRecordUnlockTimeStamp(uint64_t unlockTimeStamp, pthread_mutex_t * mutex);
    static bool debugMutexAddressInTheQueue(pthread_mutex_t * mutex);
#endif
    static void checkAndStartRecordingACriticalSection();
#ifdef OPEN_DEBUG
    static void debugPrint();
    static size_t debugReturnSize();
#endif

#ifdef COUNTING
    static void startCountCountingEvents();
    static void stopCountCountingEvents();
    static void calculateCountingDataInRealFunction();
    static void removeAbnormalCountingEventValues();
#endif

};

#endif //MMPROF_ALLOCATIONSTATUS_H