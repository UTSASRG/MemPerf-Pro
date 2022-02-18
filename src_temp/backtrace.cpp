#include "backtrace.h"

#ifdef OPEN_BACKTRACE

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
    return (void*)((uint64_t)addr-(uint64_t)link_map->l_addr);
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

void Backtrace::init() {
    BTMemMap.initialize(HashFuncs::hash_uint8_t, HashFuncs::compare_uint8_t, MAX_BT_ADDR_NUM);
    BTMemMapRecord.initialize(HashFuncs::hash_uint8_t, HashFuncs::compare_uint8_t, MAX_BT_ADDR_NUM);
}

#define OFFSET 268

uint8_t Backtrace::doABackTrace(unsigned int size) {
    void * stackTop = &size;
    void * returnAddr = (void*)*((uint64_t*)((uint64_t)stackTop+(uint64_t)OFFSET));
    uint8_t callsiteKey = (uint8_t)((ThreadLocalStatus::getStackOffset(stackTop)+(uint64_t)returnAddr)>>4);
    BackTraceMemory * status = BTMemMap.find(callsiteKey, sizeof(uint8_t));
    if(status == nullptr) {
        status = BTMemMap.insert(callsiteKey, sizeof(uint8_t), BackTraceMemory::newBackTraceMemory());
        status->numberOfFrame = backtrace(status->frames, 7);
//        numberOfCK++;

//        fprintf(stderr, "new key: %u, %p, %llu, %p, %d\n", callsiteKey, stackTop, ThreadLocalStatus::getStackOffset(stackTop), returnAddr, ThreadLocalStatus::runningThreadIndex);
//        for(int f = 4; f < status->numberOfFrame; ++f) {
//            fprintf(stderr, "frame %d, %p, %p\n", f, status->frames[f], ConvertToVMA(status->frames[f]));
//        }
//        abort();
    }
//    status->memAllocated += size;
    __atomic_add_fetch(&status->memAllocated, size, __ATOMIC_RELAXED);
//    fprintf(stderr, "key = %u, add size = %u, size = %lu\n", callsiteKey, size, status->memAllocated);
    return callsiteKey;
}

void Backtrace::subMem(uint8_t callsiteKey, unsigned int size) {
    BackTraceMemory * status = BTMemMap.find(callsiteKey, sizeof(uint8_t));
    if(!status) {
        return;
    }
    __atomic_sub_fetch(&status->memAllocated, size, __ATOMIC_RELAXED);
//    fprintf(stderr, "key = %u, sub size = %u, size = %lu\n", callsiteKey, size, status->memAllocated);
}

void Backtrace::recordMem() {
    memcpy(&BTMemMapRecord, &BTMemMap, sizeof(BTMemMap));
}

void Backtrace::printOutput() {
    uint8_t numOfBT = 0;
    BTAddrMemPair BTqueue[MAX_BT_ADDR_NUM];

    for(auto entryInHashTable: BTMemMapRecord) {
        BackTraceMemory *status = entryInHashTable.getValue();
//        if (status->memAllocated / 1024 && status->memAllocated / 1024 < ABNORMAL_VALUE) {
        if (status->memAllocated / 1024) {
            BTqueue[numOfBT].memory = status->memAllocated / 1024;
            memcpy(BTqueue[numOfBT].frames, status->frames, 7 * (sizeof(void *)));
            BTqueue[numOfBT].numberOfFrame = status->numberOfFrame;
            numOfBT++;
            if (numOfBT == 4096) {
                if (numOfBT == MAX_BT_ADDR_NUM) {
                    fprintf(stderr, "increase BTqueue length\n");
                    abort();
                }
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
}
#endif
