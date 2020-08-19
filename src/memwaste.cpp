
#include "memwaste.h"

//HashMap<void*, ObjectStatus, nolock, PrivateHeap> MemoryWaste::objStatusMap;
HashMap<void*, ObjectStatus, PrivateHeap> MemoryWaste::objStatusMap;
thread_local SizeClassSizeAndIndex MemoryWaste::currentSizeClassSizeAndIndex;
MemoryWasteStatus MemoryWaste::currentStatus, MemoryWaste::recordStatus;
MemoryWasteGlobalStatus MemoryWaste::globalStatus;
MemoryWasteTotalValue MemoryWaste::totalValue;
HashLocksSet MemoryWaste::hashLocksSet;

size_t ObjectStatus::internalFragment() {
    size_t unusedPageSize = (sizeClassSizeAndIndex.classSize - maxTouchedBytes) / PAGESIZE * PAGESIZE;
    return sizeClassSizeAndIndex.classSize - unusedPageSize - sizeClassSizeAndIndex.size;
}

ObjectStatus ObjectStatus::newObjectStatus() {
    ObjectStatus newObjectStatus;
    return newObjectStatus;
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

    internalFragment = (int64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    numOfActiveObjects.numOfAllocatedObjects = (int64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    numOfActiveObjects.numOfFreelistObjects = (int64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    numOfAccumulatedOperations.numOfNewAllocations = (uint64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    numOfAccumulatedOperations.numOfReusedAllocations = (uint64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    numOfAccumulatedOperations.numOfFree = (uint64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    blowupFlag = (int64_t*) RealX::mmap(NULL, ProgramStatus::numberOfClassSizes * sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

//    internalFragment = (int64_t*) MyMalloc::malloc(sizeOfArrays);
//    numOfActiveObjects.numOfAllocatedObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
//    numOfActiveObjects.numOfFreelistObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
//    numOfAccumulatedOperations.numOfNewAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
//    numOfAccumulatedOperations.numOfReusedAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
//    numOfAccumulatedOperations.numOfFree = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
//    blowupFlag = (int64_t*) MyMalloc::malloc(ProgramStatus::numberOfClassSizes * sizeof(uint64_t));


    memset(internalFragment, 0, sizeOfArrays);
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
            if(ProgramStatus::classSizes[classSizeIndex] == 1024)
            fprintf(stderr, "%ld ", internalFragment[arrayIndex(threadIndex, classSizeIndex)]);
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}

void MemoryWasteStatus::updateStatus(MemoryWasteStatus newStatus) {
    lock.lock();
    newStatus.cleanAbnormalValues();
    size_t sizeOfCopiedArrays = ThreadLocalStatus::totalNumOfThread * ProgramStatus::numberOfClassSizes * sizeof(uint64_t);
    memcpy(this->internalFragment, newStatus.internalFragment, sizeOfCopiedArrays);
    memcpy(this->numOfActiveObjects.numOfAllocatedObjects, newStatus.numOfActiveObjects.numOfAllocatedObjects, sizeOfCopiedArrays);
    memcpy(this->numOfActiveObjects.numOfFreelistObjects, newStatus.numOfActiveObjects.numOfFreelistObjects, sizeOfCopiedArrays);
    memcpy(this->numOfAccumulatedOperations.numOfNewAllocations, newStatus.numOfAccumulatedOperations.numOfNewAllocations, sizeOfCopiedArrays);
    memcpy(this->numOfAccumulatedOperations.numOfReusedAllocations, newStatus.numOfAccumulatedOperations.numOfReusedAllocations, sizeOfCopiedArrays);
    memcpy(this->numOfAccumulatedOperations.numOfFree, newStatus.numOfAccumulatedOperations.numOfFree, sizeOfCopiedArrays);
    memcpy(this->blowupFlag, newStatus.blowupFlag, ProgramStatus::numberOfClassSizes * sizeof(uint64_t));
    lock.unlock();
}

void MemoryWasteStatus::cleanAbnormalValues() {
    for(unsigned int threadIndex = 0; threadIndex < (unsigned int)ThreadLocalStatus::totalNumOfThread; ++threadIndex) {
        for(unsigned int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {
            if(internalFragment[arrayIndex(threadIndex, classSizeIndex)] > 0x1000000000) {
                internalFragment[arrayIndex(threadIndex, classSizeIndex)] = 0;
            }
//            if(classSizeIndex >= 1) {
//                internalFragment[arrayIndex(threadIndex, classSizeIndex)] = MIN(internalFragment[arrayIndex(threadIndex, classSizeIndex)],
//                        (int64_t)(numOfActiveObjects.numOfAllocatedObjects[arrayIndex(threadIndex, classSizeIndex)] * (ProgramStatus::classSizes[classSizeIndex] - ProgramStatus::classSizes[classSizeIndex-1])));
//            }
        }
    }
}

size_t MemoryWasteGlobalStatus::sizeOfArrays;

void MemoryWasteGlobalStatus::initialize() {
    sizeOfArrays = ProgramStatus::numberOfClassSizes * sizeof(uint64_t);

    internalFragment = (int64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    numOfActiveObjects.numOfAllocatedObjects = (int64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    numOfActiveObjects.numOfFreelistObjects = (int64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    numOfAccumulatedOperations.numOfNewAllocations = (uint64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    numOfAccumulatedOperations.numOfReusedAllocations = (uint64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    numOfAccumulatedOperations.numOfFree = (uint64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memoryBlowup = (int64_t*) RealX::mmap(NULL, sizeOfArrays, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

//    internalFragment = (int64_t*) MyMalloc::malloc(sizeOfArrays);
//    numOfActiveObjects.numOfAllocatedObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
//    numOfActiveObjects.numOfFreelistObjects = (int64_t*) MyMalloc::malloc(sizeOfArrays);
//    numOfAccumulatedOperations.numOfNewAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
//    numOfAccumulatedOperations.numOfReusedAllocations = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
//    numOfAccumulatedOperations.numOfFree = (uint64_t*) MyMalloc::malloc(sizeOfArrays);
//    memoryBlowup = (int64_t *) MyMalloc::malloc(sizeOfArrays);

    memset(internalFragment, 0, sizeOfArrays);
//    memset(numOfActiveObjects.numOfAllocatedObjects, 0, sizeOfArrays);
//    memset(numOfActiveObjects.numOfFreelistObjects, 0, sizeOfArrays);
//    memset(numOfAccumulatedOperations.numOfNewAllocations, 0, sizeOfArrays);
//    memset(numOfAccumulatedOperations.numOfReusedAllocations, 0, sizeOfArrays);
//    memset(numOfAccumulatedOperations.numOfFree, 0, sizeOfArrays);
//    memset(memoryBlowup, 0, sizeOfArrays);

}

void MemoryWasteGlobalStatus::globalize(MemoryWasteStatus recordStatus) {
//    recordStatus.debugPrint();
    for (unsigned int threadIndex = 0; threadIndex < ThreadLocalStatus::totalNumOfThread; ++threadIndex) {
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

void MemoryWasteTotalValue::clearAbnormalValues() {
    internalFragment = MIN(MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage - MemoryUsage::maxGlobalMemoryUsage.realMemoryUsage, internalFragment);
    memoryBlowup = MIN(MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage - MemoryUsage::maxGlobalMemoryUsage.realMemoryUsage - internalFragment, memoryBlowup);

}

void MemoryWasteTotalValue::getExternalFragment() {
    externalFragment = MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage - MemoryUsage::maxGlobalMemoryUsage.realMemoryUsage - internalFragment - memoryBlowup;
    externalFragment = MAX(externalFragment, 0);
}

void HashLocksSet::init() {
    for(unsigned int hashKey = 0; hashKey < MAX_OBJ_NUM; ++hashKey) {
        locks[hashKey].init();
    }
}

void HashLocksSet::lock(void *address) {
    size_t hashKey = HashFuncs::hashAddr(address, sizeof(void*)) & (MAX_OBJ_NUM-1);
    locks[hashKey].lock();
}

void HashLocksSet::unlock(void *address) {
    size_t hashKey = HashFuncs::hashAddr(address, sizeof(void*)) & (MAX_OBJ_NUM-1);
    locks[hashKey].unlock();
}

void MemoryWaste::initialize() {
    objStatusMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);
    currentStatus.initialize();
    recordStatus.initialize();
    globalStatus.initialize();
    hashLocksSet.init();
}


AllocatingTypeGotFromMemoryWaste MemoryWaste::allocUpdate(size_t size, void * address) {
    bool reused;
    ObjectStatus * status;

    hashLocksSet.lock(address);
    status = objStatusMap.find(address, sizeof(unsigned long));
    if(!status) {
        reused = false;
        currentSizeClassSizeAndIndex = ProgramStatus::getClassSizeAndIndex(size);

        status = objStatusMap.insert(address, sizeof(unsigned long), ObjectStatus::newObjectStatus(currentSizeClassSizeAndIndex, size));
        status->sizeClassSizeAndIndex = currentSizeClassSizeAndIndex;
        status->maxTouchedBytes = size;

        currentStatus.numOfAccumulatedOperations.numOfNewAllocations[arrayIndex()]++;
    }
    else {
        reused = true;
        if(status->sizeClassSizeAndIndex.size == size) {
            currentSizeClassSizeAndIndex = status->sizeClassSizeAndIndex;
        } else {
            currentSizeClassSizeAndIndex = ProgramStatus::getClassSizeAndIndex(size);
            status->sizeClassSizeAndIndex = currentSizeClassSizeAndIndex;
            if(currentSizeClassSizeAndIndex.classSize < status->maxTouchedBytes) {
                status->maxTouchedBytes = currentSizeClassSizeAndIndex.classSize;
            }
            if(size > status->maxTouchedBytes) {
                status->maxTouchedBytes = size;
            }
        }
        currentStatus.numOfActiveObjects.numOfFreelistObjects[arrayIndex()]--;
        currentStatus.numOfAccumulatedOperations.numOfReusedAllocations[arrayIndex()]++;

    }
    currentStatus.internalFragment[arrayIndex()] += (int64_t)status->internalFragment();
    hashLocksSet.unlock(address);

    currentStatus.numOfActiveObjects.numOfAllocatedObjects[arrayIndex()]++;
    if(currentStatus.blowupFlag[currentSizeClassSizeAndIndex.classSizeIndex] > 0 ) {
        currentStatus.blowupFlag[currentSizeClassSizeAndIndex.classSizeIndex]--;
    }

    return AllocatingTypeGotFromMemoryWaste{reused, currentSizeClassSizeAndIndex.classSize, currentSizeClassSizeAndIndex.classSizeIndex};
}


AllocatingTypeWithSizeGotFromMemoryWaste MemoryWaste::freeUpdate(void* address) {

    hashLocksSet.lock(address);
    /* Get old status */
    ObjectStatus* status = objStatusMap.find(address, sizeof(void *));
    if(status == nullptr) {
        hashLocksSet.unlock(address);
        return AllocatingTypeWithSizeGotFromMemoryWaste{0, AllocatingTypeGotFromMemoryWaste{false, 0, 0}};
    }
    currentSizeClassSizeAndIndex = status->sizeClassSizeAndIndex;

    currentStatus.internalFragment[arrayIndex()] -= (int64_t)status->internalFragment();
    hashLocksSet.unlock(address);

    currentStatus.numOfAccumulatedOperations.numOfFree[arrayIndex()]++;
    currentStatus.numOfActiveObjects.numOfAllocatedObjects[arrayIndex()]--;
    currentStatus.numOfActiveObjects.numOfFreelistObjects[arrayIndex()]++;


    currentStatus.blowupFlag[currentSizeClassSizeAndIndex.classSizeIndex]++;
    return AllocatingTypeWithSizeGotFromMemoryWaste{currentSizeClassSizeAndIndex.size,
                                                    AllocatingTypeGotFromMemoryWaste{false, currentSizeClassSizeAndIndex.classSize, currentSizeClassSizeAndIndex.classSizeIndex}};
}

void MemoryWaste::compareMemoryUsageAndRecordStatus(TotalMemoryUsage newTotalMemoryUsage) {
        recordStatus.updateStatus(currentStatus);
}

void MemoryWaste::globalizeRecordAndGetTotalValues() {

    globalStatus.globalize(recordStatus);
    globalStatus.calculateBlowup(recordStatus);
    globalStatus.cleanAbnormalValues();

    totalValue.getTotalValues(globalStatus);
    totalValue.clearAbnormalValues();
    totalValue.getExternalFragment();
}

void MemoryWaste::printOutput() {
    globalizeRecordAndGetTotalValues();

    GlobalStatus::printTitle((char*)"MEMORY WASTE");

    if(MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage) {

        fprintf(ProgramStatus::outputFile, "total:\t\t\t\t\t\t\t\t\t\t"
                                           "total internal fragmentation:%10ldK(%3lu%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t",
                totalValue.internalFragment/ONE_KB, totalValue.internalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);

        fprintf(ProgramStatus::outputFile, "total memory blowup:         %10ldK(%3ld%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                                           "total external fragmentation:%10ldK(%3ld%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t",
                totalValue.memoryBlowup/ONE_KB, totalValue.memoryBlowup*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage,
                totalValue.externalFragment/ONE_KB, totalValue.externalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage);

        fprintf(ProgramStatus::outputFile,
                "total real using memory:     %10ldK(%3ld%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                "total using memory:          %10ldK\n"
                "\ncurrent status:           active objects: %10lu     freelist objects: %10lu"
                "\naccumulative results:     new allocated:  %10lu     reused allocated: %10lu     freed: %10lu\n",
                MemoryUsage::maxGlobalMemoryUsage.realMemoryUsage/ONE_KB,
                100 - totalValue.internalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage
                - totalValue.memoryBlowup*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage
                - totalValue.externalFragment*100/MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage,
                MemoryUsage::maxGlobalMemoryUsage.totalMemoryUsage/ONE_KB,
                totalValue.numOfActiveObjects.numOfAllocatedObjects, totalValue.numOfActiveObjects.numOfFreelistObjects,
                totalValue.numOfAccumulatedOperations.numOfNewAllocations, totalValue.numOfAccumulatedOperations.numOfReusedAllocations,
                totalValue.numOfAccumulatedOperations.numOfFree);
    }

    fprintf(ProgramStatus::outputFile, "\n");

    for (unsigned int classSizeIndex = 0; classSizeIndex < ProgramStatus::numberOfClassSizes; ++classSizeIndex) {

        if(globalStatus.internalFragment[classSizeIndex]/ONE_KB == 0 && globalStatus.memoryBlowup[classSizeIndex]/ONE_KB == 0) {
            continue;
        }
        if(classSizeIndex != ProgramStatus::numberOfClassSizes - 1) {
            fprintf(ProgramStatus::outputFile, "size: %10lu\t\t\t", ProgramStatus::classSizes[classSizeIndex]);
        } else {
            fprintf(ProgramStatus::outputFile, "large object:   \t\t\t");
        }

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