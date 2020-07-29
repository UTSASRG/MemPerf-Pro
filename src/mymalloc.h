#ifndef MMPROF_MYMALLOC_H
#define MMPROF_MYMALLOC_H

#define PROFILER_MEMORY_SIZE 2*ONE_GB
#define MMAP_PROFILER_MEMORY_SIZE 2*ONE_GB
#define MMAP_PROFILER_HASH_MEMORY_SIZE 8*ONE_GB
#define MMAP_PROFILER_XTHREAD_MEMORY_SIZE ONE_GB

#include "real.hh"
#include "stdint.h"
#include "definevalues.h"
#include "spinlock.hh"

struct ProfilerMemory {

   char startPointer[PROFILER_MEMORY_SIZE];
    void * endPointer;
    void * overheadPointer;
    uint64_t objectNum = 0;
    spinlock lock;
    bool initialized = false;

    void initialize();
    void checkAndInitialize();
    void GetASpaceFromMemory(size_t size);
    void checkIfMemoryOutOfBound();
    void * newObjectAddr(size_t size);
    void * malloc(size_t size);
    void free(void * addr);
    bool inMemory(void * addr);
    bool ifInProfilerMemoryThenFree(void * addr);
};

struct MMAPProfilerMemory {

    void * startPointer;
    void * endPointer;
    void * overheadPointer;
    uint64_t objectNum = 0;
    size_t currentSize;
    bool initialized = false;

    void initialize(size_t setSize);
    void finalize();
    void GetASpaceFromMemory(size_t size);
    void checkIfMemoryOutOfBound();
    void * newObjectAddr(size_t size);
    void * malloc(size_t size);
    void free(void * addr);
    bool inMemory(void * addr);
    bool ifInProfilerMemoryThenFree(void * addr);
};

class MyMalloc{
private:
    static ProfilerMemory profilerMemory;
    static ProfilerMemory profilerHashMemory;
    static ProfilerMemory profilerXthreadMemory;
    static MMAPProfilerMemory threadLocalProfilerMemory[MAX_THREAD_NUMBER];
    static MMAPProfilerMemory threadLocalProfilerHashMemory[MAX_THREAD_NUMBER];
    static MMAPProfilerMemory threadLocalProfilerXthreadMemory[MAX_THREAD_NUMBER];
    static spinlock debugLock;

public:

    static void initializeForThreadLocalMemory();
    static void finalizeForThreadLocalMemory();
    static bool threadLocalMemoryInitialized();
    static void initializeForThreadLocalMemory(unsigned int threadIndex);
    static void finalizeForThreadLocalMemory(unsigned int threadIndex);
    static bool threadLocalMemoryInitialized(unsigned int threadIndex);

    static void initializeForThreadLocalHashMemory();
    static void finalizeForThreadLocalHashMemory();
    static bool threadLocalHashMemoryInitialized();
    static void initializeForThreadLocalHashMemory(unsigned int threadIndex);
    static void finalizeForThreadLocalHashMemory(unsigned int threadIndex);
    static bool threadLocalHashMemoryInitialized(unsigned int threadIndex);

    static void initializeForThreadLocalXthreadMemory();
    static void finalizeForThreadLocalXthreadMemory();
    static bool threadLocalXthreadMemoryInitialized();
    static void initializeForThreadLocalXthreadMemory(unsigned int threadIndex);
    static void finalizeForThreadLocalXthreadMemory(unsigned int threadIndex);
    static bool threadLocalXthreadMemoryInitialized(unsigned int threadIndex);

    static void * malloc(size_t size);
    static bool ifInProfilerMemoryThenFree(void * addr);
    static void * hashMalloc(size_t size);
    static void * xthreadMalloc(size_t size);
};
#endif //MMPROF_MYMALLOC_H
