#include "backtrace.h"

extern HashMap <uint64_t, BackTraceMemory, PrivateHeap> BTMemMap;
extern HashMap <uint64_t, BackTraceMemory, PrivateHeap> BTMemMapRecord;
spinlock Backtrace::lock;
spinlock Backtrace::recordLock;

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
    BTMemMap.initialize(HashFuncs::hashUnsignedlong, HashFuncs::compareUnsignedlong, MAX_BT_ADDR_NUM);
    BTMemMapRecord.initialize(HashFuncs::hashUnsignedlong, HashFuncs::compareUnsignedlong, MAX_BT_ADDR_NUM);
//    BTCttMap.initialize(HashFuncs::hashUnsignedlong, HashFuncs::compareUnsignedlong, MAX_BT_ADDR_NUM);
    lock.init();
    recordLock.init();
#endif
}

int numberOfCK = 0;

uint64_t Backtrace::doABackTrace(unsigned int size) {
#ifdef OPEN_BACKTRACE
    void * stackTop = &size;
    void * returnAddr = (void*)*((uint64_t*)((uint64_t)stackTop+(uint64_t)284));
    uint64_t callsiteKey = ThreadLocalStatus::getStackOffset(stackTop)+(uint64_t)returnAddr;
    BackTraceMemory * status = BTMemMap.find(callsiteKey, sizeof(uint64_t));
    if(status == nullptr) {
//        lock.lock();

        status = BTMemMap.insert(callsiteKey, sizeof(uint64_t), BackTraceMemory::newBackTraceMemory());
        status->numberOfFrame = backtrace(status->frames, 8);
        numberOfCK++;

//        fprintf(stderr, "%d new key: %llu, %p, %llu, %p, %d\n", numberOfCK, callsiteKey, stackTop, ThreadLocalStatus::getStackOffset(stackTop), returnAddr, ThreadLocalStatus::runningThreadIndex);
//        for(int f = 4; f < status->numberOfFrame; ++f) {
//            fprintf(stderr, "frame %d, %p, %p\n", f, status->frames[f], ConvertToVMA(status->frames[f]));
//        }
//        abort();

//        lock.unlock();
    }
    __atomic_add_fetch(&status->memAllocated, size, __ATOMIC_RELAXED);
    return callsiteKey;
#endif
}

void Backtrace::subMem(uint64_t callsiteKey, unsigned int size) {
#ifdef OPEN_BACKTRACE
    BackTraceMemory * status = BTMemMap.find(callsiteKey, sizeof(uint64_t));
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
//    for(auto entryInHashTable: BTMemMap) {
//        BackTraceMemory * status = entryInHashTable.getValue();
//        if(status->memAllocated) {
//            uint64_t frameKey = 0;
//            for(int i = 4; i < status->numberOfFrame; ++i) {
//                frameKey += (uint64_t)status->frames[i];
//            }
//            BackTraceContent * statusInCTTMap = BTCttMap.find(frameKey, sizeof(uint64_t));
//            if(statusInCTTMap == nullptr) {
//                statusInCTTMap = BTCttMap.insert(frameKey, sizeof(uint64_t), BackTraceContent::newBackTraceContent());
//                memcpy(statusInCTTMap->frames, status->frames, 8*(sizeof(void*)));
//                statusInCTTMap->numberOfFrame = status->numberOfFrame;
//            }
//            statusInCTTMap->memAllocatedAtEnd += status->memAllocated;
//        }
//    }
//
//    unsigned int numOfBT = 0;
//    BTAddrMemPair BTqueue[4096];
//    for(auto entryInHashTable: BTCttMap) {
//        BackTraceContent * BTContent = entryInHashTable.getValue();
//        if(BTContent->memAllocatedAtEnd/1024) {
//            BTqueue[numOfBT].memory = BTContent->memAllocatedAtEnd;
//            memcpy(BTqueue[numOfBT].frames, BTContent->frames, 8*(sizeof(void*)));
//            BTqueue[numOfBT].numberOfFrame = BTContent->numberOfFrame;
//            numOfBT++;
//            if(numOfBT == 4096) {
//                fprintf(stderr, "increase BTqueue length\n");
//                abort();
//            }
//        }
//    }


    unsigned short numOfBT = 0;
    BTAddrMemPair BTqueue[MAX_BT_ADDR_NUM];
    for(auto entryInHashTable: BTMemMap) {
        BackTraceMemory * status = entryInHashTable.getValue();
        if(status->memAllocated/1024) {
            BTqueue[numOfBT].memory = status->memAllocated;
            memcpy(BTqueue[numOfBT].frames, status->frames, 8*(sizeof(void*)));
            BTqueue[numOfBT].numberOfFrame = status->numberOfFrame;
            numOfBT++;
            if(numOfBT == MAX_BT_ADDR_NUM) {
                fprintf(stderr, "increase BTqueue length\n");
                abort();
            }
        }
    }

    std::sort(BTqueue, BTqueue+numOfBT, compare);

    GlobalStatus::printTitle((char*)"MEMORY LEAK BACKTRACE AT END");
    char command[512];
    for(unsigned int i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].memory) {
            fprintf(ProgramStatus::outputFile, "\n------- %uK -------\n", BTqueue[i].memory/1024);
     fflush(ProgramStatus::outputFile);
            sprintf(command, "addr2line -e %s -Ci ", ProgramStatus::programName);
            for(int f = 4; f < BTqueue[i].numberOfFrame; ++f) {
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
    fprintf(stderr, "numOfCK = %d\n", numberOfCK);

//    for(auto entryInHashTable: BTMemMapRecord) {
//        BackTraceMemory * status = entryInHashTable.getValue();
//        if(status->memAllocated) {
//            uint64_t frameKey = 0;
//            for(int i = 4; i < status->numberOfFrame; ++i) {
//                frameKey += (uint64_t)status->frames[i];
//            }
//            BackTraceContent * statusInCTTMap = BTCttMap.find(frameKey, sizeof(uint64_t));
//            if(statusInCTTMap == nullptr) {
//                statusInCTTMap = BTCttMap.insert(frameKey, sizeof(uint64_t), BackTraceContent::newBackTraceContent());
//                memcpy(statusInCTTMap->frames, status->frames, 8*(sizeof(void*)));
//                statusInCTTMap->numberOfFrame = status->numberOfFrame;
//            }
//            statusInCTTMap->memAllocatedAtPeak += status->memAllocated;
//        }
//    }
//
//    unsigned int numOfBT = 0;
//    BTAddrMemPair BTqueue[4096];
//    for(auto entryInHashTable: BTCttMap) {
//        BackTraceContent * BTContent = entryInHashTable.getValue();
//        if(BTContent->memAllocatedAtPeak/1024) {
//            BTqueue[numOfBT].memory = BTContent->memAllocatedAtPeak;
//            memcpy(BTqueue[numOfBT].frames, BTContent->frames, 8*(sizeof(void*)));
//            BTqueue[numOfBT].numberOfFrame = BTContent->numberOfFrame;
//            numOfBT++;
//            if(numOfBT == 4096) {
//                fprintf(stderr, "increase BTqueue length\n");
//                abort();
//            }
//        }
//    }

    unsigned short numOfBT = 0;
    BTAddrMemPair BTqueue[MAX_BT_ADDR_NUM];
    for(auto entryInHashTable: BTMemMapRecord) {
        BackTraceMemory * status = entryInHashTable.getValue();
        if(status->memAllocated/1024) {
            BTqueue[numOfBT].memory = status->memAllocated;
            memcpy(BTqueue[numOfBT].frames, status->frames, 8*(sizeof(void*)));
            BTqueue[numOfBT].numberOfFrame = status->numberOfFrame;
            numOfBT++;
            if(numOfBT == MAX_BT_ADDR_NUM) {
                fprintf(stderr, "increase BTqueue length\n");
                abort();
            }
        }
    }

    std::sort(BTqueue, BTqueue+numOfBT, compare);

    GlobalStatus::printTitle((char*)"MEMORY USAGE BACKTRACE AT PEAK");
    char command[512];
    for(unsigned int i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].memory/1024) {
    fflush(ProgramStatus::outputFile);
            fprintf(ProgramStatus::outputFile, "\n------- %uK -------\n", BTqueue[i].memory/1024);
            sprintf(command, "addr2line -e %s -Ci ", ProgramStatus::programName);
            for(int f = 4; f < BTqueue[i].numberOfFrame; ++f) {
                sprintf(command+strlen(command), "%p ", ConvertToVMA(BTqueue[i].frames[f]));
            }
            sprintf(command+strlen(command), "2>> %s\n", ProgramStatus::outputFileName);
            ssystem(command);
            fprintf(ProgramStatus::outputFile, "\n------------------\n");
        }
    }
#endif
}
