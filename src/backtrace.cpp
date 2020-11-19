#include "backtrace.h"

extern HashMap <uint64_t, BackTraceMemory, PrivateHeap> BTMemMap;
extern HashMap <uint64_t, BackTraceMemory, PrivateHeap> BTMemMapRecord;
//extern HashMap <uint64_t, BackTraceContent, PrivateHeap> BTCttMap;
spinlock Backtrace::lock;
spinlock Backtrace::recordLock;

BackTraceMemory BackTraceMemory::newBackTraceMemory() {
    BackTraceMemory newBackTraceMemory;
    newBackTraceMemory.memAllocated = 0;
    newBackTraceMemory.numberOfFrame = 0;
    return newBackTraceMemory;
}

//BackTraceContent BackTraceContent::newBackTraceContent() {
//    BackTraceContent newBackTraceContent;
//    newBackTraceContent.memAllocatedAtPeak = 0;
//    newBackTraceContent.memAllocatedAtEnd = 0;
//    return newBackTraceContent;
//}

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

void Backtrace::init() {
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
    BTMemMap.initialize(HashFuncs::hashUnsignedlong, HashFuncs::compareUnsignedlong, MAX_BT_ADDR_NUM);
    BTMemMapRecord.initialize(HashFuncs::hashUnsignedlong, HashFuncs::compareUnsignedlong, MAX_BT_ADDR_NUM);
//    BTCttMap.initialize(HashFuncs::hashUnsignedlong, HashFuncs::compareUnsignedlong, MAX_BT_ADDR_NUM);
    lock.init();
    recordLock.init();
#endif
}

int numberOfCK = 0;

uint64_t Backtrace::doABackTrace(size_t size) {
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
    void * stackTop = &size;
    void * returnAddr = (void*)*((uint64_t*)((uint64_t)stackTop+(uint64_t)0x160));
    uint64_t callsiteKey = ThreadLocalStatus::getStackOffset(stackTop)+(uint64_t)returnAddr;
    BackTraceMemory * status = BTMemMap.find(callsiteKey, sizeof(uint64_t));
    if(status == nullptr) {
//        lock.lock();

        status = BTMemMap.insert(callsiteKey, sizeof(uint64_t), BackTraceMemory::newBackTraceMemory());
        status->numberOfFrame = backtrace(status->frames, 8);
        numberOfCK++;
//        fprintf(stderr, "%d new key: %llu, %llu, %p, %d\n", numberOfCK, callsiteKey, ThreadLocalStatus::getStackOffset(stackTop), returnAddr, ThreadLocalStatus::runningThreadIndex);
//        for(int f = 4; f < status->numberOfFrame; ++f) {
//            fprintf(stderr, "frame %d, %p, %p\n", f, status->frames[f], ConvertToVMA(status->frames[f]));
//        }

//        lock.unlock();
    }
    status->memAllocated += size;
    return callsiteKey;
#endif
}

void Backtrace::subMem(uint64_t callsiteKey, size_t size) {
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
    BackTraceMemory * status = BTMemMap.find(callsiteKey, sizeof(uint64_t));
    if(!status) {
        return;
    }
    if (status->memAllocated >= size) {
        status->memAllocated -= size;
    } else {
        status->memAllocated = 0;
    }
#endif
}

void Backtrace::recordMem() {
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
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
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
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


    unsigned int numOfBT = 0;
    BTAddrMemPair BTqueue[4096];
    for(auto entryInHashTable: BTMemMap) {
        BackTraceMemory * status = entryInHashTable.getValue();
        if(status->memAllocated/1024) {
            BTqueue[numOfBT].memory = status->memAllocated;
            memcpy(BTqueue[numOfBT].frames, status->frames, 8*(sizeof(void*)));
            BTqueue[numOfBT].numberOfFrame = status->numberOfFrame;
            numOfBT++;
            if(numOfBT == 4096) {
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
            fprintf(ProgramStatus::outputFile, "\n------- %luK -------\n", BTqueue[i].memory/1024);
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
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
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

    unsigned int numOfBT = 0;
    BTAddrMemPair BTqueue[4096];
    for(auto entryInHashTable: BTMemMapRecord) {
        BackTraceMemory * status = entryInHashTable.getValue();
        if(status->memAllocated/1024) {
            BTqueue[numOfBT].memory = status->memAllocated;
            memcpy(BTqueue[numOfBT].frames, status->frames, 8*(sizeof(void*)));
            BTqueue[numOfBT].numberOfFrame = status->numberOfFrame;
            numOfBT++;
            if(numOfBT == 4096) {
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
            fprintf(ProgramStatus::outputFile, "\n------- %luK -------\n", BTqueue[i].memory/1024);
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
