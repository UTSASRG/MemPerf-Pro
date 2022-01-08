#include "callsite.h"

#ifndef SRC_LIGHT_CALLSITE_H
#define SRC_LIGHT_CALLSITE_H

#include "callsite.h"

extern HashMap <uint8_t, void *, PrivateHeap> callTable;

void CallKeyHashLocksSet::lock(uint8_t key) {
    size_t hashKey = HashFuncs::hash_uint8_t(key, sizeof(uint8_t)) & (NUM_CALLKEY-1);
    locks[hashKey].lock();
}

void CallKeyHashLocksSet::unlock(uint8_t key) {
    size_t hashKey = HashFuncs::hash_uint8_t(key, sizeof(uint8_t)) & (NUM_CALLKEY-1);
    locks[hashKey].unlock();
}

namespace Callsite {

    CallKeyHashLocksSet hashLocksSet;
    uint16_t numCallKey;
    bool callKeyBook[256];

    uint8_t getCallKey(uint8_t oldCallKey) {
        void * stackTop = &oldCallKey;
        void * returnAddr = (void*)*((uint64_t*)((uint64_t)stackTop + RETURN_OFFSET));
        uint8_t callKey = (uint8_t)((ThreadLocalStatus::getStackOffset(stackTop) + (uint64_t)returnAddr) >> 4);
        if(callKey != oldCallKey && !callKeyBook[callKey]) {
            hashLocksSet.lock(callKey);
            callKeyBook[callKey] = true;
            callTable.insertIfAbsent(callKey, sizeof(uint8_t), returnAddr);
            hashLocksSet.unlock(callKey);
            numCallKey++;
//                fprintf(stderr, "%lu new key: %u, %p, %llu, %p, %u\n", numCallKey, callKey, stackTop, ThreadLocalStatus::getStackOffset(stackTop), returnAddr, ThreadLocalStatus::runningThreadIndex);
        }
        return callKey;
    }

    void* ConvertToVMA(void* addr) {
        Dl_info info;
        link_map* link_map;
        int ret = dladdr1(addr, &info, (void**)&link_map, RTLD_DL_LINKMAP);
        if(ret != 1 || link_map == nullptr) {
            return nullptr;
        }
        return (void*)((uint64_t)addr-(uint64_t)link_map->l_addr);
    }

    void ssystem(char * command) {
        char path[MAX_SOURCE_LENGTH] = {0};
        FILE * fp = popen(command, "r");
        if (fp == nullptr) {
            fprintf(stderr, "popen failed. %s, with errno %d.\n", strerror(errno), errno);
        } else {
            while (!feof(fp) && fgets(path, sizeof(path), fp)) {}
            pclose(fp);
            fprintf(ProgramStatus::outputFile, "\n%s\n", path);
        }
    }

    void printCallSite(uint8_t callKey) {
        void ** callsite = callTable.find(callKey, sizeof(uint8_t));
        if(callsite && *callsite) {
            char command[512];
//            sprintf(command, "addr2line -e %s -Ci %p >> %s\n", ProgramStatus::programName, ConvertToVMA(*callsite), ProgramStatus::outputFileName);
            sprintf(command, "addr2line -e %s -Ci %p\n", ProgramStatus::programName, ConvertToVMA(*callsite));
//            fprintf(ProgramStatus::outputFile, "\n");
            ssystem(command);
//            fprintf(ProgramStatus::outputFile, "\n");
        }
    }
}

#endif //SRC_LIGHT_CALLSITE_H

