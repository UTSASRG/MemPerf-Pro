
#include "memwaste.h"

HashMap<void*, ObjectStatus, spinlock, PrivateHeap> MemoryWaste::objStatusMap;
thread_local SizeClassSizeAndIndex MemoryWaste::currentSizeClassSizeAndIndex;
MemoryWasteStatus MemoryWaste::currentStatus, MemoryWaste::recordStatus;
MemoryWasteGlobalStatus MemoryWaste::globalStatus;
MemoryWasteTotalValue MemoryWaste::totalValue;


size_t ObjectStatus::internalFragment() {
    size_t unUsedPage = (sizeClassSizeAndIndex.classSize - maxTouchedBytes) / PAGESIZE * PAGESIZE;
    return sizeClassSizeAndIndex.classSize - unUsedPage - sizeClassSizeAndIndex.size;
}

ObjectStatus ObjectStatus::newObjectStatus(SizeClassSizeAndIndex sizeClassSizeAndIndex, size_t maxTouchBytes) {
    ObjectStatus newObjectStatus;
    newObjectStatus.sizeClassSizeAndIndex = sizeClassSizeAndIndex;
    newObjectStatus.maxTouchedBytes = maxTouchBytes;
    return newObjectStatus;
}


size_t MemoryWasteStatus::sizeOfArrays;

unsigned int MemoryWasteStatus::arrayIndex(unsigned int classSizeIndex) {
    return ThreadLocalStatus::runningThreadIndex * ProgramStatus::numberOfClassSizes + classSizeIndex;
}

unsigned int MemoryWasteStatus::arrayIndex(unsigned int threadIndex, unsigned int classSizeIndex) {
    return threadIndex * ProgramStatus::numberOfClassSizes + classSizeIndex;
}

void MemoryWasteStatus::initialize() {
    sizeOfArrays = MAX_THREAD_NUMBER * ProgramStatus::numberOfClassSizes * sizeof(uint64_t);

    internalFragment = (int64_t*) MyMalloc::malloc(sizeOfArrays);
    numOfActiveObjects.numOfAllocatedObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
    numOfActiveObjects.numOfFreelistObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
    numOfAccumulatedOperations.numOfNewAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
    numOfAccumulatedOperations.numOfReusedAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
    numOfAccumulatedOperations.numOfFree = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
    blowupFlag = (int64_t*) MyMalloc::malloc(ProgramStatus::numberOfClassSizes * sizeof(uint64_t));
//
//    memset(internalFragment, 0, sizeOfArrays);
//    memset(numOfActiveObjects.numOfAllocatedObjects, 0, sizeOfArrays);
//    memset(numOfActiveObjects.numOfFreelistObjects, 0, sizeOfArrays);
//    memset(numOfAccumulatedOperations.numOfNewAllocations, 0, sizeOfArrays);
//    memset(numOfAccumulatedOperations.numOfReusedAllocations, 0, sizeOfArrays);
//    memset(numOfAccumulatedOperations.numOfFree, 0, sizeOfArrays);
//    memset(blowupFlag, 0, ProgramStatus::numberOfClassSizes * sizeof(uint64_t));

    lock.init();
}

void MemoryWasteStatus::debugPrint() {
    for(unsigned int threadIndex = 0; threadIndex < 40; ++threadIndex) {
        fprintf(stderr, "thread %d: ", threadIndex);
        for(unsigned int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
            fprintf(stderr, "%ld ", internalFragment[arrayIndex(threadIndex, classSizeIndex)]);
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}

void MemoryWasteStatus::updateStatus(MemoryWasteStatus newStatus) {
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

size_t MemoryWasteGlobalStatus::sizeOfArrays;

void MemoryWasteGlobalStatus::initialize() {
    sizeOfArrays = ProgramStatus::numberOfClassSizes * sizeof(uint64_t);

    internalFragment = (int64_t*) MyMalloc::malloc(sizeOfArrays);
    numOfActiveObjects.numOfAllocatedObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
    numOfActiveObjects.numOfFreelistObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
    numOfAccumulatedOperations.numOfNewAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
    numOfAccumulatedOperations.numOfReusedAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
    numOfAccumulatedOperations.numOfFree = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
    memoryBlowup = (int64_t *) MyMalloc::malloc(sizeOfArrays);

    memset(internalFragment, 0, sizeOfArrays);
    memset(numOfActiveObjects.numOfAllocatedObjects, 0, sizeOfArrays);
    memset(numOfActiveObjects.numOfFreelistObjects, 0, sizeOfArrays);
    memset(numOfAccumulatedOperations.numOfNewAllocations, 0, sizeOfArrays);
    memset(numOfAccumulatedOperations.numOfReusedAllocations, 0, sizeOfArrays);
    memset(numOfAccumulatedOperations.numOfFree, 0, sizeOfArrays);
    memset(memoryBlowup, 0, sizeOfArrays);

}

void MemoryWasteGlobalStatus::globalize(MemoryWasteStatus recordStatus) {
    ///Jin: bugs remaining
//    recordStatus.debugPrint();
//    for (unsigned int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
//        fprintf(stderr, "%ld ", internalFragment[classSizeIndex]);
//    }
//    fprintf(stderr, "\n");
    for (unsigned int threadIndex = 0; threadIndex < ThreadLocalStatus::totalNumOfRunningThread; ++threadIndex) {
        for (unsigned int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
            internalFragment[classSizeIndex] += recordStatus.internalFragment[MemoryWaste::arrayIndex(threadIndex, classSizeIndex)];

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
//    for (unsigned int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
//        fprintf(stderr, "%ld ", internalFragment[classSizeIndex]);
//    }
//    fprintf(stderr, "\n");
}

void MemoryWasteGlobalStatus::calculateBlowup(MemoryWasteStatus recordStatus) {
    for (unsigned int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
        memoryBlowup[classSizeIndex] = ProgramStatus::classSizes[classSizeIndex] *
                                       (numOfActiveObjects.numOfFreelistObjects[classSizeIndex] - recordStatus.blowupFlag[classSizeIndex]);
    }
}

void MemoryWasteGlobalStatus::cleanAbnormalValues() {
    for (unsigned int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
        internalFragment[classSizeIndex] = MAX(internalFragment[classSizeIndex], 0);
        numOfActiveObjects.numOfAllocatedObjects[classSizeIndex] = MAX(numOfActiveObjects.numOfAllocatedObjects[classSizeIndex], 0);
        numOfActiveObjects.numOfFreelistObjects[classSizeIndex] = MAX(numOfActiveObjects.numOfFreelistObjects[classSizeIndex], 0);
        memoryBlowup[classSizeIndex] = MAX(memoryBlowup[classSizeIndex], 0);
    }
}


void MemoryWasteTotalValue::getTotalValues(MemoryWasteGlobalStatus globalStatus) {
    for (unsigned int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
        internalFragment += globalStatus.internalFragment[classSizeIndex];

        numOfActiveObjects.numOfAllocatedObjects += globalStatus.numOfActiveObjects.numOfAllocatedObjects[classSizeIndex];
        numOfActiveObjects.numOfFreelistObjects += globalStatus.numOfActiveObjects.numOfFreelistObjects[classSizeIndex];

        numOfAccumulatedOperations.numOfNewAllocations += globalStatus.numOfAccumulatedOperations.numOfNewAllocations[classSizeIndex];
        numOfAccumulatedOperations.numOfReusedAllocations += globalStatus.numOfAccumulatedOperations.numOfReusedAllocations[classSizeIndex];
        numOfAccumulatedOperations.numOfFree += globalStatus.numOfAccumulatedOperations.numOfFree[classSizeIndex];

        memoryBlowup += globalStatus.memoryBlowup[classSizeIndex];
    }
}

void MemoryWasteTotalValue::getExternalFragment() {
    externalFragment = MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage - MemoryUsage::maxGlobalMemoryUsage.realMemoryUsage - internalFragment - memoryBlowup;
    externalFragment = MAX(externalFragment, 0);
}


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
        status = objStatusMap.insert(address, sizeof(void *), ObjectStatus::newObjectStatus(currentSizeClassSizeAndIndex, size));

        currentStatus.numOfAccumulatedOperations.numOfNewAllocations[arrayIndex()]++;
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
        currentStatus.numOfActiveObjects.numOfFreelistObjects[arrayIndex()]--;
        currentStatus.numOfAccumulatedOperations.numOfReusedAllocations[arrayIndex()]++;
    }

    currentStatus.numOfActiveObjects.numOfAllocatedObjects[arrayIndex()]++;

    currentStatus.internalFragment[arrayIndex()] += status->internalFragment();
    if(currentStatus.blowupFlag[currentSizeClassSizeAndIndex.classSizeIndex] > 0 ) {
        currentStatus.blowupFlag[currentSizeClassSizeAndIndex.classSizeIndex]--;
    }

//        fprintf(stderr, "tid = %d, alloc up size = %lu %lu %p %p\n",
//                ThreadLocalStatus::runningThreadIndex, currentSizeClassSizeAndIndex.size, status->sizeClassSizeAndIndex.size, status, &status->sizeClassSizeAndIndex.size);

    return AllocatingTypeGotFromMemoryWaste{reused, currentSizeClassSizeAndIndex.classSize};
}


AllocatingTypeWithSizeGotFromMemoryWaste MemoryWaste::freeUpdate(void* address) {

    /* Get old status */
    ObjectStatus* status = objStatusMap.find(address, sizeof(void *));
    if(status == nullptr) {
        return AllocatingTypeWithSizeGotFromMemoryWaste{0, AllocatingTypeGotFromMemoryWaste{false, 0}};
    }
    ///Jin: There are some bugs remaining
    if(status->sizeClassSizeAndIndex.size > 0x10000) {
        status->sizeClassSizeAndIndex.size %= 0x10000;
//        fprintf(stderr, "cut size = %lu\n", status->sizeClassSizeAndIndex.size);
    }
    currentSizeClassSizeAndIndex = status->sizeClassSizeAndIndex;

    currentStatus.numOfAccumulatedOperations.numOfFree[arrayIndex()]++;

    currentStatus.numOfActiveObjects.numOfAllocatedObjects[arrayIndex()]--;
    currentStatus.numOfActiveObjects.numOfFreelistObjects[arrayIndex()]++;

    currentStatus.internalFragment[arrayIndex()] -= status->internalFragment();

    currentStatus.blowupFlag[currentSizeClassSizeAndIndex.classSizeIndex]++;

    return AllocatingTypeWithSizeGotFromMemoryWaste{currentSizeClassSizeAndIndex.size,
                                                    AllocatingTypeGotFromMemoryWaste{false, currentSizeClassSizeAndIndex.classSize}};
}

void MemoryWaste::compareMemoryUsageAndRecordStatus(TotalMemoryUsage newTotalMemoryUsage) {
        recordStatus.updateStatus(currentStatus);
}

void MemoryWaste::globalizeRecordAndGetTotalValues() {

    globalStatus.globalize(recordStatus);
    globalStatus.calculateBlowup(recordStatus);
    globalStatus.cleanAbnormalValues();

    totalValue.getTotalValues(globalStatus);
    totalValue.getExternalFragment();
}

void MemoryWaste::printOutput() {
    globalizeRecordAndGetTotalValues();
    GlobalStatus::printTitle((char*)"MEMORY WASTE");
    for (unsigned int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {

        if(globalStatus.internalFragment[classSizeIndex]/ONE_KB == 0 && globalStatus.memoryBlowup[classSizeIndex]/ONE_KB == 0) {
            continue;
        }

        fprintf(ProgramStatus::outputFile, "size: %10lu\t\t\t", ProgramStatus::classSizes[classSizeIndex]);

        fprintf(ProgramStatus::outputFile, "internal fragmentation: %10ldK\t\t\tmemory blowup: %10luK\t\t\t"
                        "active alloc: %10ld\t\t\tfreelist objects: %10ld\t\t\t"
                        "new allocated: %10lu\t\t\treused allocated: %10lu\t\t\tfreed: %10lu\n",
                        globalStatus.internalFragment[classSizeIndex]/ONE_KB, globalStatus.memoryBlowup[classSizeIndex]/ONE_KB,
                globalStatus.numOfActiveObjects.numOfAllocatedObjects[classSizeIndex],
                globalStatus.numOfActiveObjects.numOfFreelistObjects[classSizeIndex],
                globalStatus.numOfAccumulatedOperations.numOfNewAllocations[classSizeIndex],
                globalStatus.numOfAccumulatedOperations.numOfReusedAllocations[classSizeIndex],
                globalStatus.numOfAccumulatedOperations.numOfFree[classSizeIndex]);
    }
    if(MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage) {
        fprintf(ProgramStatus::outputFile, "\ntotal:\t\t\t\t\t\t\t\t\t\t"
                                           "total internal fragmentation:%10ldK(%3lu%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t",
                totalValue.internalFragment/ONE_KB, totalValue.internalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);
        fprintf(ProgramStatus::outputFile, "total memory blowup:\t\t\t\t\t\t%10ldK(%3ld%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                                           "total external fragmentation:\t\t\t\t%10ldK(%3ld%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t",
                totalValue.memoryBlowup/ONE_KB, totalValue.memoryBlowup*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage,
                totalValue.externalFragment/ONE_KB, totalValue.externalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);
        fprintf(ProgramStatus::outputFile,
                "total real using memory:\t\t\t\t%10ldK(%3ld%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                "total using memory:\t\t\t\t%10ldK\n"
                "\ncurrent status:\t\t\t\t\t\t\t\t\t\t"
                "active objects: %10lu\t\t\tfreelist objects: %10lu"
                "\naccumulative results:\t\t\t\t\t\t\t\t\t\t"
                "new allocated: %10lu\t\t\treused allocated: %10lu\t\t\tfreed: %10lu\n",
                MemoryUsage::maxGlobalMemoryUsage.realMemoryUsage/ONE_KB,
                100 - totalValue.internalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage
                - totalValue.memoryBlowup*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage
                - totalValue.externalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage,
                MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage/ONE_KB,
                totalValue.numOfActiveObjects.numOfAllocatedObjects, totalValue.numOfActiveObjects.numOfFreelistObjects,
                totalValue.numOfAccumulatedOperations.numOfNewAllocations, totalValue.numOfAccumulatedOperations.numOfReusedAllocations,
                totalValue.numOfAccumulatedOperations.numOfFree);
    }

}


unsigned int MemoryWaste::arrayIndex() {
    return MemoryWasteStatus::arrayIndex(currentSizeClassSizeAndIndex.classSizeIndex);
}

unsigned int MemoryWaste::arrayIndex(unsigned int classSizeIndex) {
    return MemoryWasteStatus::arrayIndex(classSizeIndex);
}

unsigned int MemoryWaste::arrayIndex(unsigned int threadIndex, unsigned int classSizeIndex) {
    return MemoryWasteStatus::arrayIndex(threadIndex, classSizeIndex);
}