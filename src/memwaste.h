//
// Created by 86152 on 2020/2/22.
//

#ifndef MMPROF_MEMWASTE_H
#define MMPROF_MEMWASTE_H

#include <stdio.h>
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "libmallocprof.h"
#include "memsample.h"
#include "memoryusage.h"
#include "mymalloc.h"
#include "threadlocalstatus.h"
#include "allocatingstatus.h"
#include "structs.h"

class ProgramStatus;

struct ObjectStatus{
    uint64_t proof;
    SizeClassSizeAndIndex sizeClassSizeAndIndex;
    size_t maxTouchedBytes = 0;
    void * backtraceAddr = nullptr;
#ifdef PRINT_LEAK_OBJECTS
    bool allocated = false;
#endif
    size_t internalFragment();
    static ObjectStatus newObjectStatus();
    static ObjectStatus newObjectStatus(SizeClassSizeAndIndex SizeClassSizeAndIndex, size_t maxTouchBytes);
};

struct MemoryWasteStatus {
    int64_t * internalFragment;
    struct NumOfActiveObjects {
        int64_t * numOfAllocatedObjects;
        int64_t * numOfFreelistObjects;
    } numOfActiveObjects;
    struct NumOfAccumulatedOperations {
        uint64_t * numOfNewAllocations;
        uint64_t * numOfReusedAllocations;
        uint64_t * numOfFree;
    } numOfAccumulatedOperations;
    static size_t sizeOfArrays;

    static unsigned int arrayIndex(unsigned int classSizeIndex);
    static unsigned int arrayIndex(unsigned int threadIndex, unsigned int classSizeIndex);

    int64_t * blowupFlag;
    spinlock lock;

    void initialize();
    void debugPrint();
    void updateStatus(MemoryWasteStatus newStatus);
    void cleanAbnormalValues();
};

struct MemoryWasteGlobalStatus {
    int64_t * internalFragment;
    struct NumOfActiveObjects {
        int64_t * numOfAllocatedObjects;
        int64_t * numOfFreelistObjects;
    } numOfActiveObjects;
    struct NumOfAccumulatedOperations {
        uint64_t * numOfNewAllocations;
        uint64_t * numOfReusedAllocations;
        uint64_t * numOfFree;
    } numOfAccumulatedOperations;
    static size_t sizeOfArrays;

    int64_t * memoryBlowup;

    void initialize();
    void globalize(MemoryWasteStatus recordStatus);
    void calculateBlowup(MemoryWasteStatus recordStatus);
    void cleanAbnormalValues();
};

struct MemoryWasteTotalValue {
    int64_t internalFragment = 0;
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
    int64_t memoryBlowup = 0;
    void getTotalValues(MemoryWasteGlobalStatus globalStatus);
    void clearAbnormalValues();
    void getExternalFragment();
};

struct HashLocksSet {
    spinlock locks[MAX_OBJ_NUM];
    void init();
    void lock(void * address);
    void unlock(void * address);
};

class MemoryWaste{
private:
//    static HashMap <void*, ObjectStatus, nolock, PrivateHeap> objStatusMap;
//    static HashMap <void*, ObjectStatus, PrivateHeap> objStatusMap;
    static thread_local SizeClassSizeAndIndex currentSizeClassSizeAndIndex;
    static MemoryWasteStatus currentStatus, recordStatus;
    static MemoryWasteGlobalStatus globalStatus;
    static HashLocksSet hashLocksSet;


public:

    static MemoryWasteTotalValue totalValue;

    static void initialize();

    static AllocatingTypeGotFromMemoryWaste allocUpdate(size_t size, void * address, void * backtraceAddr);
    static AllocatingTypeWithSizeGotFromMemoryWaste freeUpdate(void* address);

    static void compareMemoryUsageAndRecordStatus(TotalMemoryUsage newTotalMemoryUsage);
    static void globalizeRecordAndGetTotalValues();
    static void printOutput();

    static unsigned int arrayIndex();
    static unsigned int arrayIndex(unsigned int classSizeIndex);
    static unsigned int arrayIndex(unsigned int threadIndex, unsigned int classSizeIndex);

    static void changeBlowup(unsigned int classSizeIndex, int value);
    static void changeFreelist(unsigned int classSizeIndex, int value);

    static void detectMemoryLeak();

};

#endif //MMPROF_MEMWASTE_H
