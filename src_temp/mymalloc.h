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
//#include <sys/syscall.h>
//#include "xthreadx.hh"

typedef void * threadFunction(void *);
typedef struct thread {
    pthread_t * thread;
    pid_t tid;
    threadFunction * startRoutine;
    void * startArg;
    void * result;
} thread_t;

struct ProfilerMemory {

    spinlock lock;
    bool initialized;
   char startPointer[PROFILER_MEMORY_SIZE];
    unsigned int objectNum;
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

struct XThreadMemory {
    unsigned short idx = 0;
//    xthreadx::thread_t threads[MAX_THREAD_NUMBER];
    thread_t threads[MAX_THREAD_NUMBER];
    void * malloc();
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
    static XThreadMemory profilerXthreadMemory;
    static MMAPProfilerMemory threadLocalProfilerHashMemory[MAX_THREAD_NUMBER];

public:
    static void initializeForThreadLocalHashMemory();
    static void finalizeForThreadLocalHashMemory();
    static bool threadLocalHashMemoryInitialized();
    static void initializeForThreadLocalHashMemory(unsigned int threadIndex);
    static void finalizeForThreadLocalHashMemory(unsigned int threadIndex);
    static bool threadLocalHashMemoryInitialized(unsigned int threadIndex);

    static void * malloc(size_t size);
    static bool ifInProfilerMemoryThenFree(void * addr);
    static void * hashMalloc(size_t size);
    static void * xthreadMalloc();
};
#endif //MMPROF_MYMALLOC_H
