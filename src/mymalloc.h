//
// Created by 86152 on 2020/5/20.
//

#ifndef MMPROF_MYMALLOC_H
#define MMPROF_MYMALLOC_H

#define THREAD_LOCAL_PROFILER_MEMORY_SIZE ONE_GB

#include "real.hh"
#include "stdint.h"
#include "definevalues.h"

struct ProfilerMemory {
   // void * startPointer;
   char startPointer[THREAD_LOCAL_PROFILER_MEMORY_SIZE];
    void * endPointer = (void *) ((uint64_t)startPointer + THREAD_LOCAL_PROFILER_MEMORY_SIZE);
    void * overheadPointer = startPointer;
    uint64_t objectNum = 0;

    void GetASpaceFromMemory(size_t size) {
        overheadPointer = (void *) ((uint64_t) overheadPointer + size);
        objectNum++;
    }

    void checkIfMemoryOutOfBound() {
        if(overheadPointer >= endPointer) {
            fprintf(stderr, "out of profiler memory\n");
            abort();
        }
    }

    void * newObjectAddr(size_t size) {
        return (void *) ((uint64_t)overheadPointer - size);
    }

    void * malloc(size_t size) {
        GetASpaceFromMemory(size);
        checkIfMemoryOutOfBound();
        return newObjectAddr(size);
    }

    void free(void * addr) {
        if(addr == nullptr) {
            return;
        }
        objectNum--;
        if(objectNum == 0) {
            overheadPointer = startPointer;
        }
    }

    bool inMemory(void * addr) {
        return(addr >= startPointer && addr <= endPointer);
    }
};

class MyMalloc{
private:
    static thread_local ProfilerMemory threadLocalProfilerMemory;

public:

    static void * malloc(size_t size) {
        return threadLocalProfilerMemory.malloc(size);
    }

    static void free(void* addr) {
        return threadLocalProfilerMemory.free(addr);
    }

    static bool inProfilerMemory(void * addr) {
        return threadLocalProfilerMemory.inMemory(addr);
    }
};
#endif //MMPROF_MYMALLOC_H
