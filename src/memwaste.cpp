//
// Created by 86152 on 2020/2/22.
//
//#include <atomic>
#include <stdio.h>
#include "memwaste.h"
#include "mymalloc.h"
#include "threadlocalstatus.h"
#include "programstatus.h"

void MemoryWaste::initialize() {

    objStatusMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);
    currentStatus.initialize();
    recordStatus.initialize();
    globalStatus.initialize();
}


AllocatingTypeGotFromMemoryWaste MemoryWaste::allocUpdate(size_t size, void * address) {

    bool reused;
    ObjectStatus * status = objStatusMap.find(address, sizeof(unsigned long));
    if(!status) {
        reused = false;
        currentSizeClassSizeAndIndex = ProgramStatus::getClassSizeAndIndex(size);
        status = objStatusMap.insert(address, sizeof(void *), objStatus{currentSizeClassSizeAndIndex, size});

        currentStatus.numOfAccumulatedOperations.numOfNewAllocations[MemoryWasteStatus::arrayIndex()]++;
    }
    else {
        reused = true;
        if(status->sizeClassSizeAndIndex.size == size) {
            currentSizeClassSizeAndIndex = status->sizeClassSizeAndIndex;
        } else {
            currentSizeClassSizeAndIndex = ProgramStatus::getClassSizeAndIndex(size);
            status->sizeClassSizeAndIndex = currentSizeClassSizeAndIndex;
            if(size > status->maxTouchedBytes) {
                status->maxTouchedBytes = size;
            }
        }
        currentStatus.numOfActiveObjects.numOfFreelistObjects[MemoryWasteStatus::arrayIndex()]--;
        currentStatus.numOfAccumulatedOperations.numOfReusedAllocations[MemoryWasteStatus::arrayIndex()]++;
    }

    currentStatus.numOfActiveObjects.numOfAllocatedObjects[MemoryWasteStatus::arrayIndex()]++;

    currentStatus.internalFragment[MemoryWasteStatus::arrayIndex()] += status.internalFragement();
    if(currentStatus.blowupFlag[currentSizeClassSizeAndIndex.classSizeIndex] > 0 ) {
        currentStatus.blowupFlag[currentSizeClassSizeAndIndex.classSizeIndex]--;
    }

    return AllocatingTypeGotFromMemoryWaste{reused, currentSizeClassSizeAndIndex.classSize};
}


AllocatingTypeWithSizeGotFromMemoryWaste MemoryWaste::freeUpdate(void* address) {

    /* Get old status */
    ObjectStatus* status = objStatusMap.find(address, sizeof(void *));

    currentSizeClassSizeAndIndex = status->sizeClassSizeAndIndex;

    currentStatus.numOfAccumulatedOperations.numOfFree[MemoryWasteStatus::arrayIndex()]++;

    currentStatus.numOfActiveObjects.numOfAllocatedObjects[MemoryWasteStatus::arrayIndex()]--;
    currentStatus.numOfActiveObjects.numOfFreelistObjects[MemoryWasteStatus::arrayIndex()]++;

    currentStatus.internalFragment[MemoryWasteStatus::arrayIndex()] -= status->internalFragment();

    currentStatus.blowupFlag[currentSizeClassSizeAndIndex.classSizeIndex]++;

    return AllocatingTypeWithSizeGotFromMemoryWaste{currentSizeClassSizeAndIndex.size,
                                                    AllocatingTypeGotFromMemoryWaste{false, currentSizeClassSizeAndIndex.classSize}};
}


void MemoryWaste::compareMemoryUsageAndRecordStatus(TotalMemoryUsage newTotalMemoryUsage) {
        recordStatus.updateStatus(currentStatus);
}


void MemoryWaste::globalizeRecordAndGetTotalValues() {
    globalStatus.globalize();
    globalStatus.calculateBlowup();
    globalStatus.cleanAbnormalValues();

    totalValue.getTotalValues();
    totalValue.getExternalFragment();
}


void MemoryWaste::printOutput() {
    GlobalStatus::printTitle("MEMORY WASTE");
    for (int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {

        if(globalStatus.internalFragment[classSizeIndex] == 0 && globalStatus.memoryBlowup[classSizeIndex] == 0) {
            continue;
        }

        fprintf(ProgramStatus::outputFile, "size: %10lu\t\t\t", ProgramStatus::classSizes[classSizeIndex]);

        fprintf(ProgramStatus::outputFile, "internal fragmentation: %10luK\t\t\tmemory blowup: %10luK\t\t\t"
                        "active alloc: %10ld\t\t\tfreelist objects: %10ld\t\t\t"
                        "new allocated: %10lu\t\t\treused allocated: %10lu\t\t\tfreed: %10lu\n",
                        globalStatus.internalFragment[i]/ONE_KB, globalStatus.memoryBlowup/ONE_KB,
                globalStatus.numOfActiveObjects.numOfAllocatedObjects[i],
                globalStatus.numOfActiveObjects.numOfFreelistObjects[i],
                globalStatus.numOfAccumulatedOperations.numOfNewAllocations[i],
                globalStatus.numOfAccumulatedOperations.numOfReusedAllocations[i],
                globalStatus.numOfAccumulatedOperations.numOfFree[i]);
    }

    fprintf(ProgramStatus::outputFile, "\ntotal:\t\t\t\t\t\t\t\t\t\t"
                    "total internal fragmentation:%10luK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t",
            totalValue.internalFragment/ONE_KB, totalValue.internalFragment*100/maxTotalMemoryUsage.totalMemoryUsage);
        fprintf(ProgramStatus::outputFile, "total memory blowup:\t\t\t\t\t\t%10ldK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                        "total external fragmentation:\t\t\t\t%10ldK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t",
                totalValue.memoryBlowup/ONE_KB, totalValue.memoryBlowup*100/maxTotalMemoryUsage.totalMemoryUsage,
                totalValue.externalFragment/ONE_KB, totalValue.externalFragment*100/maxTotalMemoryUsage.totalMemoryUsage);
    fprintf(ProgramStatus::outputFile,
            "total real using memory:\t\t\t\t%10ldK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                    "total using memory:\t\t\t\t%10ldK\n"
                    "\ncurrent status:\t\t\t\t\t\t\t\t\t\t"
                    "active objects: %10lu\t\t\tfreelist objects: %10lu"
                    "\naccumulative results:\t\t\t\t\t\t\t\t\t\t"
                    "new allocated: %10lu\t\t\treused allocated: %10lu\t\t\tfreed: %10lu\n",
            maxTotalMemoryUsage.realMemoryUsage/ONE_KB,
            100 - totalValue.internalFragment*100/maxTotalMemoryUsage.totalMemoryUsage - totalValue.memoryBlowup*100/maxTotalMemoryUsage.totalMemoryUsage - totalValue.externalFragment*100/totalMem,
            maxTotalMemoryUsage.totalMemoryUsage/ONE_KB,
            totalValue.numOfActiveObjects.numOfAllocatedObjects, totalValue.numOfActiveObjects.numOfFreelistObjects,
            totalValue.numOfAccumulatedOperations.numOfNewAllocations, totalValue.numOfAccumulatedOperations.numOfReusedAllocations,
            totalValue.numOfAccumulatedOperations.numOfFree);

}

