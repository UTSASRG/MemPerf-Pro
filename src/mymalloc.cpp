#include "mymalloc.h"
#include "threadlocalstatus.h"

void ProfilerMemory::initialize() {
    endPointer = (void *) ((uint64_t)startPointer + PROFILER_MEMORY_SIZE);
    overheadPointer = startPointer;
//    lock.init();
    initialized = true;
}

void ProfilerMemory::checkAndInitialize() {
    if(!initialized) initialize();
}

void ProfilerMemory::GetASpaceFromMemory(size_t size) {
    overheadPointer = (void *) ((uint64_t) overheadPointer + size);
    objectNum++;
}

//void ProfilerMemory::checkIfMemoryOutOfBound() {
//    if(overheadPointer >= endPointer) {
//        fprintf(stderr, "out of profiler memory\n");
//        abort();
//    }
//}

void * ProfilerMemory::newObjectAddr(size_t size) {
    return (void *) ((uint64_t)overheadPointer - size);
}

void * ProfilerMemory::malloc(size_t size) {
    lock.lock();
    checkAndInitialize();
    GetASpaceFromMemory(size);
//    checkIfMemoryOutOfBound();
    void * object = newObjectAddr(size);
    lock.unlock();
    return object;
}

void * ProfilerMemory::mallocWithoutLock(size_t size) {
//    lock.lock();
    checkAndInitialize();
    GetASpaceFromMemory(size);
//    checkIfMemoryOutOfBound();
    void * object = newObjectAddr(size);
//    lock.unlock();
    return object;
}

//void ProfilerMemory::free(void * addr) {
//    if(addr == nullptr) {
//        return;
//    }
//    objectNum--;
//    if(objectNum == 0) {
//        overheadPointer = startPointer;
//    }
//}

bool ProfilerMemory::inMemory(void * addr) {
    return(addr >= startPointer && addr <= endPointer);
}

bool ProfilerMemory::ifInProfilerMemoryThenFree(void * addr) {
    checkAndInitialize();
    if(inMemory(addr)) {
//        free(addr);
        return true;
    }
    return false;
}


void MMAPProfilerMemory::initialize(size_t setSize) {
    currentSize = setSize;
    startPointer = RealX::mmap(NULL, currentSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    endPointer = (void *) ((uint64_t)startPointer + currentSize);
    overheadPointer = startPointer;
    initialized = true;
}

void MMAPProfilerMemory::finalize() {
    RealX::munmap(overheadPointer, (uint64_t)endPointer - (uint64_t)overheadPointer);
    initialized = false;
}

void MMAPProfilerMemory::GetASpaceFromMemory(size_t size) {
    overheadPointer = (void *) ((uint64_t) overheadPointer + size);
    objectNum++;
}

//void MMAPProfilerMemory::checkIfMemoryOutOfBound() {
//    if(overheadPointer >= endPointer) {
//        fprintf(stderr, "MMAPProfilerMemory used up\n");
//        abort();
//    }
//}

void * MMAPProfilerMemory::newObjectAddr(size_t size) {
    return (void *) ((uint64_t)overheadPointer - size);
}

void * MMAPProfilerMemory::malloc(size_t size) {
    GetASpaceFromMemory(size);
//    checkIfMemoryOutOfBound();
    return newObjectAddr(size);
}

//void MMAPProfilerMemory::free(void * addr) {
//    if(addr == nullptr) {
//        return;
//    }
//    objectNum--;
//    if(objectNum == 0) {
//        overheadPointer = startPointer;
//    }
//}

bool MMAPProfilerMemory::inMemory(void * addr) {
    return(addr >= startPointer && addr <= endPointer);
}

bool MMAPProfilerMemory::ifInProfilerMemoryThenFree(void * addr) {
    if(inMemory(addr)) {
//        free(addr);
        return true;
    }
    return false;
}


ProfilerMemory MyMalloc::profilerMemory;
//ProfilerMemory MyMalloc::profilerHashMemory;
ProfilerMemory MyMalloc::profilerXthreadMemory;
#ifdef ENABLE_PRECISE_BLOWUP
ProfilerMemory MyMalloc::profilerShadowMemory;
#endif
//MMAPProfilerMemory MyMalloc::threadLocalProfilerMemory[MAX_THREAD_NUMBER];
MMAPProfilerMemory MyMalloc::threadLocalProfilerHashMemory[MAX_THREAD_NUMBER];
//MMAPProfilerMemory MyMalloc::threadLocalProfilerXthreadMemory[MAX_THREAD_NUMBER];
#ifdef ENABLE_PRECISE_BLOWUP
MMAPProfilerMemory MyMalloc::threadLocalProfilerShadowMemory[MAX_THREAD_NUMBER];
#endif

#ifdef OPEN_DEBUG
spinlock MyMalloc::debugLock;
#endif

//void MyMalloc::initializeForThreadLocalMemory() {
//    threadLocalProfilerMemory[ThreadLocalStatus::runningThreadIndex].initialize(MMAP_PROFILER_MEMORY_SIZE);
//}
//
//void MyMalloc::finalizeForThreadLocalMemory() {
//    threadLocalProfilerMemory[ThreadLocalStatus::runningThreadIndex].finalize();
//}
//
//bool MyMalloc::threadLocalMemoryInitialized() {
//    return threadLocalProfilerMemory[ThreadLocalStatus::runningThreadIndex].initialized;
//}
//
//void MyMalloc::initializeForThreadLocalMemory(unsigned int threadIndex) {
//    threadLocalProfilerMemory[threadIndex].initialize(MMAP_PROFILER_MEMORY_SIZE);
//}
//
//void MyMalloc::finalizeForThreadLocalMemory(unsigned int threadIndex) {
//    threadLocalProfilerMemory[threadIndex].finalize();
//}
//
//bool MyMalloc::threadLocalMemoryInitialized(unsigned int threadIndex) {
//    return threadLocalProfilerMemory[threadIndex].initialized;
//}





void MyMalloc::initializeForThreadLocalHashMemory() {
    threadLocalProfilerHashMemory[ThreadLocalStatus::runningThreadIndex].initialize(MMAP_PROFILER_HASH_MEMORY_SIZE);
}

void MyMalloc::finalizeForThreadLocalHashMemory() {
    threadLocalProfilerHashMemory[ThreadLocalStatus::runningThreadIndex].finalize();
}

bool MyMalloc::threadLocalHashMemoryInitialized() {
    return threadLocalProfilerHashMemory[ThreadLocalStatus::runningThreadIndex].initialized;
}

void MyMalloc::initializeForThreadLocalHashMemory(unsigned int threadIndex) {
    threadLocalProfilerHashMemory[threadIndex].initialize(MMAP_PROFILER_HASH_MEMORY_SIZE);
}

void MyMalloc::finalizeForThreadLocalHashMemory(unsigned int threadIndex) {
    threadLocalProfilerHashMemory[threadIndex].finalize();
}

bool MyMalloc::threadLocalHashMemoryInitialized(unsigned int threadIndex) {
    return threadLocalProfilerHashMemory[threadIndex].initialized;
}
//
//
//
//void MyMalloc::initializeForThreadLocalXthreadMemory() {
//    threadLocalProfilerXthreadMemory[ThreadLocalStatus::runningThreadIndex].initialize(MMAP_PROFILER_XTHREAD_MEMORY_SIZE);
//}
//
//void MyMalloc::finalizeForThreadLocalXthreadMemory() {
//    threadLocalProfilerXthreadMemory[ThreadLocalStatus::runningThreadIndex].finalize();
//}

//bool MyMalloc::threadLocalXthreadMemoryInitialized() {
//    return threadLocalProfilerXthreadMemory[ThreadLocalStatus::runningThreadIndex].initialized;
//}

//void MyMalloc::initializeForThreadLocalXthreadMemory(unsigned int threadIndex) {
//    threadLocalProfilerXthreadMemory[threadIndex].initialize(MMAP_PROFILER_XTHREAD_MEMORY_SIZE);
//}
//
//void MyMalloc::finalizeForThreadLocalXthreadMemory(unsigned int threadIndex) {
//    threadLocalProfilerXthreadMemory[threadIndex].finalize();
//}

//bool MyMalloc::threadLocalXthreadMemoryInitialized(unsigned int threadIndex) {
//    return threadLocalProfilerXthreadMemory[threadIndex].initialized;
//}

#ifdef ENABLE_PRECISE_BLOWUP
void MyMalloc::initializeForThreadLocalShadowMemory() {
    threadLocalProfilerShadowMemory[ThreadLocalStatus::runningThreadIndex].initialize(MMAP_PROFILER_SHADOW_MEMORY_SIZE);
}

void MyMalloc::finalizeForThreadLocalShadowMemory() {
    threadLocalProfilerShadowMemory[ThreadLocalStatus::runningThreadIndex].finalize();
}

bool MyMalloc::threadLocalShadowMemoryInitialized() {
    return threadLocalProfilerShadowMemory[ThreadLocalStatus::runningThreadIndex].initialized;
}

void MyMalloc::initializeForThreadLocalShadowMemory(unsigned int threadIndex) {
    threadLocalProfilerShadowMemory[threadIndex].initialize(MMAP_PROFILER_SHADOW_MEMORY_SIZE);
}

void MyMalloc::finalizeForThreadLocalShadowMemory(unsigned int threadIndex) {
    threadLocalProfilerShadowMemory[threadIndex].finalize();
}

bool MyMalloc::threadLocalShadowMemoryInitialized(unsigned int threadIndex) {
    return threadLocalProfilerShadowMemory[threadIndex].initialized;
}
#endif

void * MyMalloc::malloc(size_t size) {
//    if(threadLocalMemoryInitialized()) {
//        return threadLocalProfilerMemory[ThreadLocalStatus::runningThreadIndex].malloc(size);
//    }
//    return profilerMemory.malloc(size);
    return profilerMemory.mallocWithoutLock(size);
}

bool MyMalloc::ifInProfilerMemoryThenFree(void * addr) {
//    if(threadLocalMemoryInitialized()) {
//        if(threadLocalProfilerMemory[ThreadLocalStatus::runningThreadIndex].ifInProfilerMemoryThenFree(addr)) {
//            return true;
//        }
//    }
//    for(unsigned short threadIndex = 0; threadIndex < ThreadLocalStatus::totalNumOfThread; ++threadIndex) {
//        if(threadIndex != ThreadLocalStatus::runningThreadIndex && threadLocalMemoryInitialized(threadIndex)) {
//            if(threadLocalProfilerMemory[threadIndex].ifInProfilerMemoryThenFree(addr)) {
//                return true;
//            }
//        }
//    }
    return profilerMemory.ifInProfilerMemoryThenFree(addr);
}

void * MyMalloc::hashMalloc(size_t size) {
    void * object;
//    if(threadLocalHashMemoryInitialized()) {
        object = threadLocalProfilerHashMemory[ThreadLocalStatus::runningThreadIndex].malloc(size);
//    } else {
//        object = profilerHashMemory.malloc(size);
//    }
    return object;
}

void * MyMalloc::xthreadMalloc(size_t size) {
    void * object;
//    if(threadLocalXthreadMemoryInitialized()) {
//        object = threadLocalProfilerXthreadMemory[ThreadLocalStatus::runningThreadIndex].malloc(size);
//    } else {
        object = profilerXthreadMemory.mallocWithoutLock(size);
//    object = profilerMemory.mallocWithoutLock(size);

//    }
    return object;
}
#ifdef ENABLE_PRECISE_BLOWUP
void * MyMalloc::shadowMalloc(size_t size) {
    void * object;
    if(threadLocalShadowMemoryInitialized()) {
        object = threadLocalProfilerShadowMemory[ThreadLocalStatus::runningThreadIndex].malloc(size);
    } else {
        object = profilerShadowMemory.malloc(size);
    }
    return object;
}
#endif