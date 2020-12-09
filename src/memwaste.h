
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
    bool allocated = false;
    bool mark = false;
#ifdef OPEN_BACKTRACE
    uint8_t callsiteKey = 0;
#endif
    unsigned short tid;
    SizeClassSizeAndIndex sizeClassSizeAndIndex;
    unsigned int maxTouchedBytes = 0;
    size_t internalFragment();
//    static ObjectStatus newObjectStatus();
    static ObjectStatus newObjectStatus(SizeClassSizeAndIndex SizeClassSizeAndIndex, unsigned int maxTouchBytes);
};

struct MemoryWasteStatus {
    int64_t * internalFragment;
    struct NumOfActiveObjects {
        int * numOfAllocatedObjects;
        int * numOfFreelistObjects;
    } numOfActiveObjects;
    struct NumOfAccumulatedOperations {
        unsigned int * numOfNewAllocations;
        unsigned int * numOfReusedAllocations;
        unsigned int * numOfFree;
    } numOfAccumulatedOperations;
    static size_t sizeOfArrays;

    static unsigned int arrayIndex(unsigned short classSizeIndex);
    static unsigned int arrayIndex(unsigned short threadIndex, unsigned short classSizeIndex);

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
        int * numOfAllocatedObjects;
        int * numOfFreelistObjects;
    } numOfActiveObjects;
    struct NumOfAccumulatedOperations {
        unsigned int * numOfNewAllocations;
        unsigned int * numOfReusedAllocations;
        unsigned int * numOfFree;
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
        int numOfAllocatedObjects = 0;
        int numOfFreelistObjects = 0;
    } numOfActiveObjects;
    struct NumOfAccumulatedOperations {
        unsigned int numOfNewAllocations = 0;
        unsigned int numOfReusedAllocations = 0;
        unsigned int numOfFree = 0;
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


public:

    static HashLocksSet hashLocksSet;
#ifdef PRINT_LEAK_OBJECTS
    static unsigned long minAddr;
    static unsigned long maxAddr;
#endif

    static MemoryWasteTotalValue totalValue;

    static void initialize();

    static AllocatingTypeGotFromMemoryWaste allocUpdate(unsigned int size, void * address, uint8_t callsiteKey);
    static AllocatingTypeWithSizeGotFromMemoryWaste freeUpdate(void* address);

    static void compareMemoryUsageAndRecordStatus(TotalMemoryUsage newTotalMemoryUsage);
    static void globalizeRecordAndGetTotalValues();
    static void printOutput();

    static unsigned int arrayIndex();
    static unsigned int arrayIndex(unsigned short classSizeIndex);
    static unsigned int arrayIndex(unsigned short threadIndex, unsigned short classSizeIndex);
#ifdef ENABLE_PRECISE_BLOWUP
    static void changeBlowup(unsigned short classSizeIndex, int value);
    static void changeFreelist(unsigned short classSizeIndex, int value);
#endif
//    static void detectMemoryLeak();

};

#endif //MMPROF_MEMWASTE_H
