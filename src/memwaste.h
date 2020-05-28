//
// Created by 86152 on 2020/2/22.
//

#ifndef MMPROF_MEMWASTE_H
#define MMPROF_MEMWASTE_H


//#include <atomic>
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "libmallocprof.h"
#include "memsample.h"
#include "programstatus.h"
#include "memoryusage.h"


class MemoryWaste{
private:

    struct ObjectStatus{
        SizeClassSizeAndIndex sizeClassSizeAndIndex;
        size_t maxTouchedBytes = 0;
        size_t internalFragment() {
            size_t unUsedPage = (sizeClassSizeAndIndex.classSize - maxTouchedBytes) / PAGESIZE * PAGESIZE;
            return sizeClassSizeAndIndex.classSize - unUsedPage - sizeClassSizeAndIndex.size;
        };
    };
    static HashMap <void*, ObjectStatus, spinlock, PrivateHeap> objStatusMap;

    static thread_local SizeClassSizeAndIndex currentSizeClassSizeAndIndex;

    static struct MemoryWasteStatus {
        uint64_t * internalFragment;
        struct NumOfActiveObjects {
            int64_t * numOfAllocatedObjects;
            int64_t * numOfFreelistObjects;
        } numOfActiveObjects;
        struct NumOfAccumulatedOperations {
            uint64_t * numOfNewAllocations;
            uint64_t * numOfReusedAllocations;
            uint64_t * numOfFree;
        } numOfAccumulatedOperations;

        size_t sizeOfArrays = MAX_THREAD_NUMBER * ProgramStatus::numberOfClassSizes * sizeof(uint64_t);
        static unsigned int arrayIndex() {
            return ThreadLocalStatus::runningThreadIndex * ProgramStatus::numberOfClassSizes + currentSizeClassSizeAndIndex.classSizeIndex;
        }
        static unsigned int arrayIndex(unsigned int classSizeIndex) {
            return ThreadLocalStatus::runningThreadIndex * ProgramStatus::numberOfClassSizes + classSizeIndex;
        }
        static unsigned int arrayIndex(unsigned int threadIndex, unsigned int classSizeIndex) {
            return threadIndex * ProgramStatus::numberOfClassSizes + classSizeIndex;
        }

        int64_t * blowupFlag;
        spinlock lock;

        void initialize() {
            internalFragment = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
            numOfActiveObjects.numOfAllocatedObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
            numOfActiveObjects.numOfFreelistObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
            numOfAccumulatedOperations.numOfNewAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
            numOfAccumulatedOperations.numOfReusedAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
            numOfAccumulatedOperations.numOfFree = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
            blowupFlag = (int64_t*) MyMalloc::malloc(ProgramStatus::numberOfClassSizes * sizeof(uint64_t));
            lock.init();
        }

        void updateStatus(MemoryWasteStatus newStatus) {
            lock.lock();
            memcpy(this->internalFragment, newStatus.internalFragment, sizeOfArrays);
            memcpy(this->numOfActiveObjects.numOfAllocatedObjects, newStatus.numOfActiveObjects.numOfAllocatedObjects, sizeOfArrays);
            memcpy(this->numOfActiveObjects.numOfFreelistObjects, newStatus.numOfActiveObjects.numOfFreelistObjects, sizeOfArrays);
            memcpy(this->numOfAccumulatedOperations.numOfNewAllocations, newStatus.numOfAccumulatedOperations.numOfNewAllocations, sizeOfArrays);
            memcpy(this->numOfAccumulatedOperations.numOfReusedAllocations, newStatus.numOfAccumulatedOperations.numOfReusedAllocations, sizeOfArrays);
            memcpy(this->numOfAccumulatedOperations.numOfFree, newStatus.numOfAccumulatedOperations.numOfFree, sizeOfArrays);
            memcpy(this->blowupFlag, newStatus.blowupFlag, ProgramStatus::numberOfClassSizes * sizeof(uint64_t));
            lock.unlock();
        }
    } currentStatus, recordStatus;

    static struct MemoryWasteGlobalStatus {
        uint64_t * internalFragment;
        struct NumOfActiveObjects {
            int64_t * numOfAllocatedObjects;
            int64_t * numOfFreelistObjects;
        } numOfActiveObjects;
        struct NumOfAccumulatedOperations {
            uint64_t * numOfNewAllocations;
            uint64_t * numOfReusedAllocations;
            uint64_t * numOfFree;
        } numOfAccumulatedOperations;
        size_t sizeOfArrays = ProgramStatus::numberOfClassSizes * sizeof(uint64_t);

        int64_t * memoryBlowup;

        void initialize() {
            internalFragment = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
            numOfActiveObjects.numOfAllocatedObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
            numOfActiveObjects.numOfFreelistObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
            numOfAccumulatedOperations.numOfNewAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
            numOfAccumulatedOperations.numOfReusedAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
            numOfAccumulatedOperations.numOfFree = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
            memoryBlowup = (int64_t *) MyMalloc::malloc(sizeOfArrays);
        }

        void globalize() {
            for (int threadIndex = 0; threadIndex < ThreadLocalStatus::totalNumOfRunningThread; ++threadIndex) {
                for (int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
                    internalFragment[classSizeIndex] += recordStatus.internalFragment[MemoryWasteStatus::arrayIndex(threadIndex, classSizeIndex)];

                    numOfActiveObjects.numOfAllocatedObjects[classSizeIndex] +=
                            recordStatus.numOfActiveObjects.numOfAllocatedObjects[MemoryWasteStatus::arrayIndex(threadIndex, classSizeIndex)];
                    numOfActiveObjects.numOfFreelistObjects[classSizeIndex] +=
                            recordStatus.numOfActiveObjects.numOfFreelistObjects[MemoryWasteStatus::arrayIndex(threadIndex, classSizeIndex)];

                    numOfAccumulatedOperations.numOfNewAllocations[classSizeIndex] +=
                            recordStatus.numOfAccumulatedOperations.numOfNewAllocations[MemoryWasteStatus::arrayIndex(threadIndex, classSizeIndex)];
                    numOfAccumulatedOperations.numOfReusedAllocations[classSizeIndex] +=
                            recordStatus.numOfAccumulatedOperations.numOfReusedAllocations[MemoryWasteStatus::arrayIndex(threadIndex, classSizeIndex)];
                    numOfAccumulatedOperations.numOfFree[classSizeIndex] +=
                            recordStatus.numOfAccumulatedOperations.numOfFree[MemoryWasteStatus::arrayIndex(threadIndex, classSizeIndex)];

                }
            }
        }

        void calculateBlowup() {
            for (int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
                memoryBlowup[classSizeIndex] = ProgramStatus::classSizes[classSizeIndex] *
                                         (numOfActiveObjects.numOfFreelistObjects[classSizeIndex] - recordStatus.blowupFlag[classSizeIndex]);

            }
        }

        void cleanAbnormalValues() {
            for (int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
                internalFragment[classSizeIndex] = MAX(internalFragment[classSizeIndex], 0);
                numOfActiveObjects.numOfAllocatedObjects[classSizeIndex] = MAX(numOfActiveObjects.numOfAllocatedObjects[classSizeIndex], 0);
                numOfActiveObjects.numOfFreelistObjects[classSizeIndex] = MAX(numOfActiveObjects.numOfFreelistObjects[classSizeIndex], 0);
                memoryBlowup[classSizeIndex] = MAX(memoryBlowup[classSizeIndex], 0);
            }
        }

    } globalStatus;

    static struct MemoryWasteTotalValue {
        uint64_t internalFragment = 0;
        struct NumOfActiveObjects {
            int64_t numOfAllocatedObjects = 0;
            int64_t numOfFreelistObjects = 0;
        } numOfActiveObjects;
        struct NumOfAccumulatedOperations {
            uint64_t numOfNewAllocations = 0;
            uint64_t numOfReusedAllocations = 0;
            uint64_t numOfFree = 0;
        } numOfAccumulatedOperations;
        int64_t externalFragment;
        uint64_t memoryBlowup = 0;
        void getTotalValues() {
            for (int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
                internalFragment += globalStatus.internalFragment[classSizeIndex];

                numOfActiveObjects.numOfAllocatedObjects += globalStatus.numOfActiveObjects.numOfAllocatedObjects[classSizeIndex];
                numOfActiveObjects.numOfFreelistObjects += globalStatus.numOfActiveObjects.numOfFreelistObjects[classSizeIndex];

                numOfAccumulatedOperations.numOfNewAllocations += globalStatus.numOfAccumulatedOperations.numOfNewAllocations[classSizeIndex];
                numOfAccumulatedOperations.numOfReusedAllocations += globalStatus.numOfAccumulatedOperations.numOfReusedAllocations[classSizeIndex];
                numOfAccumulatedOperations.numOfFree += globalStatus.numOfAccumulatedOperations.numOfFree[classSizeIndex];

                memoryBlowup += globalStatus.memoryBlowup[classSizeIndex];
            }
        }
        void getExternalFragment() {
            externalFragment = maxTotalMemoryUsage.totalMemoryUsage - maxTotalMemoryUsage.realMemoryUsage - internalFragment - memoryBlowup;
            externalFragment = MAX(externalFragment, 0);
        }
    } totalValue;


public:

    static void initialize();

    static AllocatingTypeGotFromMemoryWaste allocUpdate(size_t size, void * address);
    static AllocatingTypeWithSizeGotFromMemoryWaste freeUpdate(void* address);

    static void compareMemoryUsageAndRecordStatus(TotalMemoryUsage newTotalMemoryUsage);
    static void globalizeRecordAndGetTotalValues();
    static void reportMaxMemory(FILE * output);
};

#endif //MMPROF_MEMWASTE_H
