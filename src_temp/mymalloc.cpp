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

void * ProfilerMemory::newObjectAddr(size_t size) {
    return (void *) ((uint64_t)overheadPointer - size);
}

void * ProfilerMemory::malloc(size_t size) {
    lock.lock();
    checkAndInitialize();
    GetASpaceFromMemory(size);
    void * object = newObjectAddr(size);
    lock.unlock();
    return object;
}

void * ProfilerMemory::mallocWithoutLock(size_t size) {
    checkAndInitialize();
    GetASpaceFromMemory(size);
    void * object = newObjectAddr(size);
    return object;
}

bool ProfilerMemory::inMemory(void * addr) {
    return(addr >= startPointer && addr <= endPointer);
}

bool ProfilerMemory::ifInProfilerMemoryThenFree(void * addr) {
    checkAndInitialize();
    if(inMemory(addr)) {
        return true;
    }
    return false;
}

void * XThreadMemory::malloc() {
    return &threads[idx++];
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


void * MMAPProfilerMemory::newObjectAddr(size_t size) {
    return (void *) ((uint64_t)overheadPointer - size);
}

void * MMAPProfilerMemory::malloc(size_t size) {
    GetASpaceFromMemory(size);
    return newObjectAddr(size);
}

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
XThreadMemory MyMalloc::profilerXthreadMemory;
MMAPProfilerMemory MyMalloc::threadLocalProfilerHashMemory[MAX_THREAD_NUMBER];


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

void * MyMalloc::malloc(size_t size) {
    return profilerMemory.mallocWithoutLock(size);
}

bool MyMalloc::ifInProfilerMemoryThenFree(void * addr) {
    return profilerMemory.ifInProfilerMemoryThenFree(addr);
}

void * MyMalloc::hashMalloc(size_t size) {
    void * object = threadLocalProfilerHashMemory[ThreadLocalStatus::runningThreadIndex].malloc(size);
    return object;
}

void * MyMalloc::xthreadMalloc() {
    void * object;
    object = profilerXthreadMemory.malloc();
    return object;
}