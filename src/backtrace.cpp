#include "backtrace.h"

extern HashMap <uint8_t, BackTraceMemory, PrivateHeap> BTMemMap;
extern HashMap <uint8_t, BackTraceMemory, PrivateHeap> BTMemMapRecord;
//spinlock Backtrace::lock;
//spinlock Backtrace::recordLock;

BackTraceMemory BackTraceMemory::newBackTraceMemory() {
    BackTraceMemory newBackTraceMemory;
    newBackTraceMemory.memAllocated = 0;
    newBackTraceMemory.numberOfFrame = 0;
    return newBackTraceMemory;
}

bool Backtrace::compare(BTAddrMemPair a, BTAddrMemPair b) {
    return a.memory > b.memory;
}

void* Backtrace::ConvertToVMA(void* addr)
{
    Dl_info info;
    link_map* link_map;
    dladdr1(addr, &info, (void**)&link_map, RTLD_DL_LINKMAP);
    return addr-link_map->l_addr;
}

void Backtrace::ssystem(char *command) {
    char path[1035];
    FILE * fp = popen(command, "r");
    if (fp == nullptr) {
        printf("Failed to run command\n" );
    } else {
        while (fgets(path, sizeof(path), fp)) {
            fprintf(ProgramStatus::outputFile, "%s", path);
        }
        pclose(fp);
    }
}
#ifdef OPEN_DEBUG
void Backtrace::debugPrintTrace() {
    char **strings;
    enum Constexpr { MAX_SIZE = 4 };
    void *array[MAX_SIZE];
    backtrace(array, MAX_SIZE);
    strings = backtrace_symbols(&array[3], 1);
    fprintf(stderr, "%s\n", strings[0]);
    if(strings) {
        fprintf(stderr, "\n");
        free(strings);
    }
}
#endif
void Backtrace::init() {
#ifdef OPEN_BACKTRACE
    BTMemMap.initialize(HashFuncs::hashCharInt, HashFuncs::compareCharInt, MAX_BT_ADDR_NUM);
    BTMemMapRecord.initialize(HashFuncs::hashCharInt, HashFuncs::compareCharInt, MAX_BT_ADDR_NUM);
//    BTCttMap.initialize(HashFuncs::hashUnsignedlong, HashFuncs::compareUnsignedlong, MAX_BT_ADDR_NUM);
//    lock.init();
//    recordLock.init();
#endif
}

//uint8_t numberOfCK = 0;
uint8_t keys[MAX_BT_ADDR_NUM];
uint8_t numkeys = 0;
spinlock debuglock;

//void * returnAddr = (void*)*((uint64_t*)((uint64_t)stackTop+(uint64_t)284));
//void * returnAddr = (void*)*((uint64_t*)((uint64_t)stackTop+(uint64_t)252));
uint8_t Backtrace::doABackTrace(unsigned int size) {
#ifdef OPEN_BACKTRACE
    void * stackTop = &size;
    void * returnAddr = (void*)*((uint64_t*)((uint64_t)stackTop+(uint64_t)268));
    uint8_t callsiteKey = (uint8_t)(ThreadLocalStatus::getStackOffset(stackTop)+(uint64_t)returnAddr);
    BackTraceMemory * status = BTMemMap.find(callsiteKey, sizeof(uint8_t));
    if(status == nullptr) {
//        lock.lock();

        debuglock.lock();
        keys[numkeys++] = callsiteKey;
        debuglock.unlock();
        status = BTMemMap.insert(callsiteKey, sizeof(uint8_t), BackTraceMemory::newBackTraceMemory());


        status->numberOfFrame = backtrace(status->frames, 7);
//        numberOfCK++;

//        fprintf(stderr, "%d new key: %llu, %p, %llu, %p, %d\n", numberOfCK, callsiteKey, stackTop, ThreadLocalStatus::getStackOffset(stackTop), returnAddr, ThreadLocalStatus::runningThreadIndex);
//        for(int f = 4; f < status->numberOfFrame; ++f) {
//            fprintf(stderr, "frame %d, %p, %p\n", f, status->frames[f], ConvertToVMA(status->frames[f]));
//        }
//        abort();

//        lock.unlock();
    }
//    else {
//        debuglock.unlock();
//    }
    __atomic_add_fetch(&status->memAllocated, size, __ATOMIC_RELAXED);
//    fprintf(stderr, "%p, %lu, key = %lu size = %lu %u %d\n", status, MemoryUsage::maxRealMemoryUsage, callsiteKey, status->memAllocated, size, BTMemMap.getEntryNumber());
    return callsiteKey;
#endif
}

void Backtrace::subMem(uint8_t callsiteKey, unsigned int size) {
#ifdef OPEN_BACKTRACE
    BackTraceMemory * status = BTMemMap.find(callsiteKey, sizeof(uint8_t));
    if(!status) {
        return;
    }
    __atomic_sub_fetch(&status->memAllocated, size, __ATOMIC_RELAXED);
#endif
}

void Backtrace::recordMem() {
#ifdef OPEN_BACKTRACE
    memcpy(&BTMemMapRecord, &BTMemMap, sizeof(BTMemMap));
//    for(auto entryInHashTable: BTMemMap) {
//        BackTraceMemory newStatus = *(entryInHashTable.getValue());
//        if(newStatus.memAllocated) {
//            uint64_t callsiteKey = entryInHashTable.getKey();
//            BackTraceMemory * status = BTMemMapRecord.find(callsiteKey, sizeof(uint64_t));
//            if(status) {
//                status->memAllocated = newStatus.memAllocated;
//            } else {
//                BTMemMapRecord.insert(callsiteKey, sizeof(uint64_t), newStatus);
//            }
//        }
//    }
#endif
}

void Backtrace::debugPrintOutput() {
#ifdef OPEN_BACKTRACE
    fprintf(stderr, "numkeys = %d\n", numkeys);
    uint8_t numOfBT = 0;
    BTAddrMemPair BTqueue[MAX_BT_ADDR_NUM];
    for(uint8_t i = 0; i < numkeys; ++i) {
        BackTraceMemory * status = BTMemMap.find(keys[i], sizeof(uint8_t));
        if(status->memAllocated/1024 && status->memAllocated < MemoryUsage::maxRealMemoryUsage) {
            BTqueue[numOfBT].memory = status->memAllocated/1024;
            memcpy(BTqueue[numOfBT].frames, status->frames, 7*(sizeof(void*)));
            BTqueue[numOfBT].numberOfFrame = status->numberOfFrame;
            numOfBT++;
            if(numOfBT >= MAX_BT_ADDR_NUM) {
                fprintf(stderr, "increase BTqueue length\n");
                abort();
            }
        }
    }

    std::sort(BTqueue, BTqueue+numOfBT, compare);

    GlobalStatus::printTitle((char*)"MEMORY LEAK BACKTRACE AT END");
    char command[512];
    for(uint8_t i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].memory) {
            fprintf(ProgramStatus::outputFile, "\n------- %uK -------\n", BTqueue[i].memory);
     fflush(ProgramStatus::outputFile);
            sprintf(command, "addr2line -e %s -Ci ", ProgramStatus::programName);
            for(uint8_t f = 4; f < BTqueue[i].numberOfFrame; ++f) {
                sprintf(command+strlen(command), "%p ", ConvertToVMA(BTqueue[i].frames[f]));
            }
            sprintf(command+strlen(command), "2>> %s\n", ProgramStatus::outputFileName);
            ssystem(command);
            fprintf(ProgramStatus::outputFile, "\n------------------\n");
        }
    }

#endif
}

void Backtrace::printOutput() {
#ifdef OPEN_BACKTRACE
    uint8_t numOfBT = 0;
    BTAddrMemPair BTqueue[MAX_BT_ADDR_NUM];
    for(uint8_t i = 0; i < numkeys; ++i) {
        BackTraceMemory * status = BTMemMap.find(keys[i], sizeof(uint8_t));
        if(status && status->memAllocated/1024 && status->memAllocated < MemoryUsage::maxRealMemoryUsage) {
            BTqueue[numOfBT].memory = status->memAllocated/1024;
            memcpy(BTqueue[numOfBT].frames, status->frames, 7*(sizeof(void*)));
            BTqueue[numOfBT].numberOfFrame = status->numberOfFrame;
            numOfBT++;
            if(numOfBT >= MAX_BT_ADDR_NUM) {
                fprintf(stderr, "increase BTqueue length\n");
                abort();
            }
        }
    }

    std::sort(BTqueue, BTqueue+numOfBT, compare);

    GlobalStatus::printTitle((char*)"MEMORY USAGE BACKTRACE AT PEAK");
    char command[512];
    for(uint8_t i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].memory) {
    fflush(ProgramStatus::outputFile);
            fprintf(ProgramStatus::outputFile, "\n------- %uK -------\n", BTqueue[i].memory);
            sprintf(command, "addr2line -e %s -Ci ", ProgramStatus::programName);
            for(uint8_t f = 4; f < BTqueue[i].numberOfFrame; ++f) {
                sprintf(command+strlen(command), "%p ", ConvertToVMA(BTqueue[i].frames[f]));
            }
            sprintf(command+strlen(command), "2>> %s\n", ProgramStatus::outputFileName);
            ssystem(command);
            fprintf(ProgramStatus::outputFile, "\n------------------\n");
        }
    }
#endif
}
