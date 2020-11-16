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
    dladdr1((void*)addr, &info, (void**)&link_map, RTLD_DL_LINKMAP);
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
    BTMemMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_BT_ADDR_NUM);
    BTMemMapRecord.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_BT_ADDR_NUM);
    lock.init();
    recordLock.init();
#endif
}

void * Backtrace::doABackTrace(size_t size) {
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
    void * stackTop = &size;
    void * BKAddr = (void*)*((uint64_t*)((uint64_t)stackTop+(uint64_t)0x140F0));
    BackTraceMemory * status = BTMemMap.findOrAdd(BKAddr, sizeof(void*), BackTraceMemory::newBackTraceMemory());
    status->memAllocated[ThreadLocalStatus::runningThreadIndex] += size;
    return BKAddr;
#endif
}

void Backtrace::subMem(void *addr, size_t size) {
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
    BackTraceMemory * status = BTMemMap.find(addr, sizeof(void*));
    if (status->memAllocated[ThreadLocalStatus::runningThreadIndex] >= size) {
        status->memAllocated[ThreadLocalStatus::runningThreadIndex] -= size;
    } else {
        status->memAllocated[ThreadLocalStatus::runningThreadIndex] = 0;
    }
#endif
}

void Backtrace::recordMem() {
#ifdef RANDOM_PERIOD_FOR_BACKTRACE
    for(auto entryInHashTable: BTMemMap) {
        void * addr = entryInHashTable.getKey();
        BackTraceMemory newStatus = *(entryInHashTable.getValue());
        BackTraceMemory * status = BTMemMapRecord.findOrAdd(addr, sizeof(void*), newStatus);
        if(status) {
            memcpy(status->memAllocated, newStatus.memAllocated, sizeof(size_t) * ThreadLocalStatus::totalNumOfThread);
        }
    }
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

    GlobalStatus::printTitle((char*)"MEMORY LEAK BACKTRACE AT END");

    if(!numOfBT) {
        return;
    }

    for(unsigned int i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].address && BTqueue[i].memory/1024) {
            fprintf(ProgramStatus::outputFile, "%zx, %s: %luK\n", ConvertToVMA((size_t)BTqueue[i].address), backtrace_symbols(&BTqueue[i].address, 1)[0], BTqueue[i].memory/1024);
        }
    }

    char command[512];
    sprintf(command, "addr2line -e %s -Ci ", ProgramStatus::programName);
    for(unsigned int i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].address && BTqueue[i].memory) {
            sprintf(command+strlen(command), "%zx ", ConvertToVMA((size_t)BTqueue[i].address));
        }
    }
    sprintf(command+strlen(command), "2>> %s\n", ProgramStatus::outputFileName);
    fflush(ProgramStatus::outputFile);
    ssystem(command);
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

    GlobalStatus::printTitle((char*)"MEMORY USAGE AT PEAK");

    if(!numOfBT) {
        return;
    }

    for(unsigned int i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].address && BTqueue[i].memory/1024) {
            fprintf(ProgramStatus::outputFile, "%zx, %s: %luK\n", ConvertToVMA((size_t)BTqueue[i].address), backtrace_symbols(&BTqueue[i].address, 1)[0], BTqueue[i].memory/1024);
        }
    }

    char command[512];
    sprintf(command, "addr2line -e %s -Ci ", ProgramStatus::programName);
    for(unsigned int i = 0; i < MIN(5, numOfBT); ++i) {
        if(BTqueue[i].address && BTqueue[i].memory) {
            sprintf(command+strlen(command), "%zx ", ConvertToVMA((size_t)BTqueue[i].address));
        }
    }
    sprintf(command+strlen(command), "2>> %s\n", ProgramStatus::outputFileName);
    fflush(ProgramStatus::outputFile);
    ssystem(command);
    fflush(ProgramStatus::outputFile);
#endif
}
