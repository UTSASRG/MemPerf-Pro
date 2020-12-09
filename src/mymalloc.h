#ifndef MMPROF_MYMALLOC_H
#define MMPROF_MYMALLOC_H

#define PROFILER_MEMORY_SIZE 2*ONE_GB
#define MMAP_PROFILER_MEMORY_SIZE 2*ONE_GB
#define MMAP_PROFILER_HASH_MEMORY_SIZE 8*ONE_GB
//#define MMAP_PROFILER_XTHREAD_MEMORY_SIZE ONE_GB
#define MMAP_PROFILER_SHADOW_MEMORY_SIZE ONE_GB

#include "real.hh"
#include "stdint.h"
#include "definevalues.h"
#include "spinlock.hh"

struct ProfilerMemory {

    spinlock lock;
    bool initialized = false;
   char startPointer[PROFILER_MEMORY_SIZE];
    unsigned int objectNum = 0;
    void * endPointer;
    void * overheadPointer;

    void initialize();
    void checkAndInitialize();
    void GetASpaceFromMemory(size_t size);
//    void checkIfMemoryOutOfBound();
    void * newObjectAddr(size_t size);
    void * malloc(size_t size);
    void * mallocWithoutLock(size_t size);
//    void free(void * addr);
    bool inMemory(void * addr);
    bool ifInProfilerMemoryThenFree(void * addr);
};

struct MMAPProfilerMemory {

    bool initialized = false;
    unsigned int objectNum = 0;
    void * startPointer;
    void * endPointer;
    void * overheadPointer;
    size_t currentSize;


    void initialize(size_t setSize);
    void finalize();
    void GetASpaceFromMemory(size_t size);
//    void checkIfMemoryOutOfBound();
    void * newObjectAddr(size_t size);
    void * malloc(size_t size);
//    void free(void * addr);
    bool inMemory(void * addr);
    bool ifInProfilerMemoryThenFree(void * addr);
};

class MyMalloc{
private:
    static ProfilerMemory profilerMemory;
//    static ProfilerMemory profilerHashMemory;
    static ProfilerMemory profilerXthreadMemory;
#ifdef ENABLE_PRECISE_BLOWUP
    static ProfilerMemory profilerShadowMemory;
#endif
//    static MMAPProfilerMemory threadLocalProfilerMemory[MAX_THREAD_NUMBER];
    static MMAPProfilerMemory threadLocalProfilerHashMemory[MAX_THREAD_NUMBER];
//    static MMAPProfilerMemory threadLocalProfilerXthreadMemory[MAX_THREAD_NUMBER];
#ifdef ENABLE_PRECISE_BLOWUP
    static MMAPProfilerMemory threadLocalProfilerShadowMemory[MAX_THREAD_NUMBER];
#endif
#ifdef OPEN_DEBUG
    static spinlock debugLock;
#endif

public:

//    static void initializeForThreadLocalMemory();
//    static void finalizeForThreadLocalMemory();
//    static bool threadLocalMemoryInitialized();
//    static void initializeForThreadLocalMemory(unsigned int threadIndex);
//    static void finalizeForThreadLocalMemory(unsigned int threadIndex);
//    static bool threadLocalMemoryInitialized(unsigned int threadIndex);

    static void initializeForThreadLocalHashMemory();
    static void finalizeForThreadLocalHashMemory();
    static bool threadLocalHashMemoryInitialized();
    static void initializeForThreadLocalHashMemory(unsigned int threadIndex);
    static void finalizeForThreadLocalHashMemory(unsigned int threadIndex);
    static bool threadLocalHashMemoryInitialized(unsigned int threadIndex);

//    static void initializeForThreadLocalXthreadMemory();
//    static void finalizeForThreadLocalXthreadMemory();
//    static bool threadLocalXthreadMemoryInitialized();
//    static void initializeForThreadLocalXthreadMemory(unsigned int threadIndex);
//    static void finalizeForThreadLocalXthreadMemory(unsigned int threadIndex);
//    static bool threadLocalXthreadMemoryInitialized(unsigned int threadIndex);
#ifdef ENABLE_PRECISE_BLOWUP
    static void initializeForThreadLocalShadowMemory();
    static void finalizeForThreadLocalShadowMemory();
    static bool threadLocalShadowMemoryInitialized();
    static void initializeForThreadLocalShadowMemory(unsigned int threadIndex);
    static void finalizeForThreadLocalShadowMemory(unsigned int threadIndex);
    static bool threadLocalShadowMemoryInitialized(unsigned int threadIndex);
#endif

    static void * malloc(size_t size);
    static bool ifInProfilerMemoryThenFree(void * addr);
    static void * hashMalloc(size_t size);
    static void * xthreadMalloc(size_t size);
#ifdef ENABLE_PRECISE_BLOWUP
    static void * shadowMalloc(size_t size);
#endif
};
#endif //MMPROF_MYMALLOC_H
