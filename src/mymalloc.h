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

   char startPointer[THREAD_LOCAL_PROFILER_MEMORY_SIZE];
    void * endPointer;
    void * overheadPointer;
    uint64_t objectNum = 0;
    bool initialized = false;

    void initialize() {
        endPointer = (void *) ((uint64_t)startPointer + THREAD_LOCAL_PROFILER_MEMORY_SIZE);
        overheadPointer = startPointer;
        initialized = true;
    }

    void checkAndInitialize() {
        if(!initialized) initialize();
    }

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
        checkAndInitialize();
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

    bool ifInProfilerMemoryThenFree(void * addr) {
        checkAndInitialize();
        if(inMemory(addr)) {
            free(addr);
            return true;
        }
        return false;
    }
};

struct ThreadLocalProfilerMemory {

    void * startPointer;
    void * endPointer;
    void * overheadPointer;
    uint64_t objectNum = 0;
    bool initialized = false;

    void initialize() {
        startPointer = RealX::mmap(NULL, THREAD_LOCAL_PROFILER_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        endPointer = (void *) ((uint64_t)startPointer + THREAD_LOCAL_PROFILER_MEMORY_SIZE);
        overheadPointer = startPointer;
        initialized = true;
    }

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

    bool ifInProfilerMemoryThenFree(void * addr) {
        if(inMemory(addr)) {
            free(addr);
            return true;
        }
        return false;
    }
};

class MyMalloc{
private:
    static ProfilerMemory profilerMemory;
    static ProfilerMemory profilerHashMemory;
    static thread_local ThreadLocalProfilerMemory threadLocalProfilerMemory;

public:

    static void initializeForThreadLocalMemory() {
        threadLocalProfilerMemory.initialize();
    }

    static bool threadLocalMemoryInitialized() {
        return threadLocalProfilerMemory.initialized;
    }

    static void * malloc(size_t size) {
        if(threadLocalMemoryInitialized()) {
            return threadLocalProfilerMemory.malloc(size);
        }
        return profilerMemory.malloc(size);
    }

    static bool ifInProfilerMemoryThenFree(void * addr) {
        bool freed = false;
        if(threadLocalMemoryInitialized()) {
            freed = threadLocalProfilerMemory.ifInProfilerMemoryThenFree(addr);
        }
        freed = ( profilerMemory.ifInProfilerMemoryThenFree(addr) || profilerHashMemory.ifInProfilerMemoryThenFree(addr) );
        return freed;
    }

    static void * hashMalloc(size_t size) {
        return profilerHashMemory.malloc(size);
    }
};
#endif //MMPROF_MYMALLOC_H
