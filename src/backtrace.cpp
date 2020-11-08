#include "backtrace.h"

extern HashMap <void*, BackTraceMemory, PrivateHeap> BTMemMap;
extern HashMap <void*, BackTraceMemory, PrivateHeap> BTMemMapRecord;

spinlock Backtrace::lock;
spinlock Backtrace::recordLock;

BackTraceMemory BackTraceMemory::newBackTraceMemory() {
    BackTraceMemory newBackTraceMemory;
    memset(newBackTraceMemory.memAllocated, 0, sizeof(size_t)*MAX_THREAD_NUMBER);
    return newBackTraceMemory;
}

bool Backtrace::compare(BTAddrMemPair a, BTAddrMemPair b) {
    return a.memory > b.memory;
}

size_t Backtrace::ConvertToVMA(size_t addr)
{
    Dl_info info;
    link_map* link_map;
    dladdr1((void*)addr,&info,(void**)&link_map,RTLD_DL_LINKMAP);
    return addr-link_map->l_addr;
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
    BTMemMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_BT_ADDR_NUM);
    BTMemMapRecord.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_BT_ADDR_NUM);
    lock.init();
    recordLock.init();
#endif
}

void * Backtrace::doABackTrace(size_t size) {
    void * stackTop = &size;
    void * BKAddr = (void*)*((uint64_t*)((uint64_t)stackTop+(uint64_t)0x140E0));
//    lock.lock();
    BackTraceMemory * status = BTMemMap.findOrAdd(BKAddr, sizeof(void*), BackTraceMemory::newBackTraceMemory());
//    lock.unlock();
    status->memAllocated[ThreadLocalStatus::runningThreadIndex] += size;
    return BKAddr;
}

void Backtrace::subMem(void *addr, size_t size) {
//    lock.lock();
    BackTraceMemory * status = BTMemMap.find(addr, sizeof(void*));
//    lock.unlock();
    if (status->memAllocated[ThreadLocalStatus::runningThreadIndex] >= size) {
        status->memAllocated[ThreadLocalStatus::runningThreadIndex] -= size;
    } else {
        status->memAllocated[ThreadLocalStatus::runningThreadIndex] = 0;
    }
}

void Backtrace::recordMem() {
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
//    recordLock.lock();
    for(auto entryInHashTable: BTMemMap) {
        void * addr = entryInHashTable.getKey();
        BackTraceMemory newStatus = *(entryInHashTable.getValue());
        BackTraceMemory * status = BTMemMapRecord.findOrAdd(addr, sizeof(void*), newStatus);
        if(status) {
            memcpy(status->memAllocated, newStatus.memAllocated, sizeof(size_t) * ThreadLocalStatus::totalNumOfThread);
        }
    }
//    recordLock.unlock();
#endif
}

void Backtrace::debugPrintOutput() {
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
    unsigned int numOfBT = 0;
    BTAddrMemPair BTqueue[MAX_BT_ADDR_NUM];
    for(auto entryInHashTable: BTMemMap) {
        void * address = entryInHashTable.getKey();
        BackTraceMemory * BTMem = entryInHashTable.getValue();
        for(unsigned int i = 0; i < ThreadLocalStatus::totalNumOfThread; ++i) {
            BTqueue[numOfBT].memory += BTMem->memAllocated[i];
        }
        BTqueue[numOfBT].address = address;
        numOfBT++;
    }
    std::sort(BTqueue, BTqueue+numOfBT, compare);

    GlobalStatus::printTitle((char*)"BACKTRACE OF SOURCE CODES");
    for(unsigned int i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].address && BTqueue[i].memory) {
            fprintf(ProgramStatus::outputFile, "%zx, %s: %llu\n", ConvertToVMA((size_t)BTqueue[i].address), backtrace_symbols(&BTqueue[i].address, 1)[0], BTqueue[i].memory);
        }
    }
    fflush(ProgramStatus::outputFile);
    for(unsigned int i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].address && BTqueue[i].memory) {
            char command[256];
            snprintf(command, sizeof(command), "addr2line -e %s -Ci %zx >> %s\n", ProgramStatus::programName, ConvertToVMA((size_t)BTqueue[i].address), ProgramStatus::outputFileName);
            system(command);
        }
    }
    fflush(ProgramStatus::outputFile);
#endif
}

void Backtrace::printOutput() {
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
    unsigned int numOfBT = 0;
    BTAddrMemPair BTqueue[MAX_BT_ADDR_NUM];
    for(auto entryInHashTable: BTMemMapRecord) {
        void * address = entryInHashTable.getKey();
        BackTraceMemory * BTMem = entryInHashTable.getValue();
        for(unsigned int i = 0; i < ThreadLocalStatus::totalNumOfThread; ++i) {
            BTqueue[numOfBT].memory += BTMem->memAllocated[i];
        }
        BTqueue[numOfBT].address = address;
        numOfBT++;
    }
    std::sort(BTqueue, BTqueue+numOfBT, compare);

    GlobalStatus::printTitle((char*)"BACKTRACE OF SOURCE CODES");

    if(!numOfBT) {
        return;
    }

    for(unsigned int i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].address && BTqueue[i].memory) {
            fprintf(ProgramStatus::outputFile, "%zx, %s: %llu\n", ConvertToVMA((size_t)BTqueue[i].address), backtrace_symbols(&BTqueue[i].address, 1)[0], BTqueue[i].memory);
        }
    }

    char command[512];
    sprintf(command, "addr2line -e %s -Ci ", ProgramStatus::programName);
    for(unsigned int i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].address && BTqueue[i].memory) {
            sprintf(command+strlen(command), "%zx ", ConvertToVMA((size_t)BTqueue[i].address));
        }
    }
    sprintf(command+strlen(command), ">> %s\n", ProgramStatus::outputFileName);
    fflush(ProgramStatus::outputFile);
    system(command);
    fflush(ProgramStatus::outputFile);
//    fprintf(stderr, "%s\n", command);
#endif
}