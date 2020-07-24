#include "mymalloc.h"
#include "threadlocalstatus.h"

void ProfilerMemory::initialize() {
    endPointer = (void *) ((uint64_t)startPointer + PROFILER_MEMORY_SIZE);
    overheadPointer = startPointer;
    lock.init();
    initialized = true;
}

void ProfilerMemory::checkAndInitialize() {
    if(!initialized) initialize();
}

void ProfilerMemory::GetASpaceFromMemory(size_t size) {
    overheadPointer = (void *) ((uint64_t) overheadPointer + size);
    objectNum++;
}

void ProfilerMemory::checkIfMemoryOutOfBound() {
    if(overheadPointer >= endPointer) {
        fprintf(stderr, "out of profiler memory\n");
        abort();
    }
}

void * ProfilerMemory::newObjectAddr(size_t size) {
    return (void *) ((uint64_t)overheadPointer - size);
}

void * ProfilerMemory::malloc(size_t size) {
    checkAndInitialize();
    GetASpaceFromMemory(size);
    checkIfMemoryOutOfBound();
    void * object = newObjectAddr(size);
    return object;
}

void ProfilerMemory::free(void * addr) {
    if(addr == nullptr) {
        return;
    }
    objectNum--;
    if(objectNum == 0) {
        overheadPointer = startPointer;
    }
}

bool ProfilerMemory::inMemory(void * addr) {
    return(addr >= startPointer && addr <= endPointer);
}

bool ProfilerMemory::ifInProfilerMemoryThenFree(void * addr) {
    checkAndInitialize();
    if(inMemory(addr)) {
        free(addr);
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
}

void MMAPProfilerMemory::GetASpaceFromMemory(size_t size) {
    overheadPointer = (void *) ((uint64_t) overheadPointer + size);
    objectNum++;
}

void MMAPProfilerMemory::checkIfMemoryOutOfBound() {
    if(overheadPointer >= endPointer) {
        fprintf(stderr, "MMAPProfilerMemory used up\n");
        abort();
    }
}

void * MMAPProfilerMemory::newObjectAddr(size_t size) {
    return (void *) ((uint64_t)overheadPointer - size);
}

void * MMAPProfilerMemory::malloc(size_t size) {
    GetASpaceFromMemory(size);
    checkIfMemoryOutOfBound();
    return newObjectAddr(size);
}

void MMAPProfilerMemory::free(void * addr) {
    if(addr == nullptr) {
        return;
    }
    objectNum--;
    if(objectNum == 0) {
        overheadPointer = startPointer;
    }
}

bool MMAPProfilerMemory::inMemory(void * addr) {
    return(addr >= startPointer && addr <= endPointer);
}

bool MMAPProfilerMemory::ifInProfilerMemoryThenFree(void * addr) {
    if(inMemory(addr)) {
        free(addr);
        return true;
    }
    return false;
}


ProfilerMemory MyMalloc::profilerMemory;
ProfilerMemory MyMalloc::profilerHashMemory;
//thread_local MMAPProfilerMemory MyMalloc::threadLocalProfilerMemory;
MMAPProfilerMemory MyMalloc::threadLocalProfilerMemory[MAX_THREAD_NUMBER];
MMAPProfilerMemory MyMalloc::MMAPProfilerHashMemory[MAX_THREAD_NUMBER];
spinlock MyMalloc::debugLock;

void MyMalloc::initializeForThreadLocalMemory() {
    threadLocalProfilerMemory[ThreadLocalStatus::runningThreadIndex].initialize(MMAP_PROFILER_MEMORY_SIZE);
}

void MyMalloc::finalizeForThreadLocalMemory() {
    threadLocalProfilerMemory[ThreadLocalStatus::runningThreadIndex].finalize();
}

bool MyMalloc::threadLocalMemoryInitialized() {
    return threadLocalProfilerMemory[ThreadLocalStatus::runningThreadIndex].initialized;
}

void MyMalloc::initializeForThreadLocalMemory(unsigned int threadIndex) {
    threadLocalProfilerMemory[threadIndex].initialize(MMAP_PROFILER_MEMORY_SIZE);
}

void MyMalloc::finalizeForThreadLocalMemory(unsigned int threadIndex) {
    threadLocalProfilerMemory[threadIndex].finalize();
}

bool MyMalloc::threadLocalMemoryInitialized(unsigned int threadIndex) {
    return threadLocalProfilerMemory[threadIndex].initialized;
}

void MyMalloc::initializeForMMAPHashMemory() {
    MMAPProfilerHashMemory[ThreadLocalStatus::runningThreadIndex].initialize(MMAP_PROFILER_HASH_MEMORY_SIZE);
}

void MyMalloc::finalizeForMMAPHashMemory() {
    MMAPProfilerHashMemory[ThreadLocalStatus::runningThreadIndex].finalize();
}

bool MyMalloc::MMAPHashMemoryInitialized() {
    return MMAPProfilerHashMemory[ThreadLocalStatus::runningThreadIndex].initialized;
}

void MyMalloc::initializeForMMAPHashMemory(unsigned int threadIndex) {
    MMAPProfilerHashMemory[threadIndex].initialize(MMAP_PROFILER_HASH_MEMORY_SIZE);
}

void MyMalloc::finalizeForMMAPHashMemory(unsigned int threadIndex) {
    MMAPProfilerHashMemory[threadIndex].finalize();
}

bool MyMalloc::MMAPHashMemoryInitialized(unsigned int threadIndex) {
    return MMAPProfilerHashMemory[threadIndex].initialized;
}

void * MyMalloc::malloc(size_t size) {
    if(threadLocalMemoryInitialized()) {
        return threadLocalProfilerMemory[ThreadLocalStatus::runningThreadIndex].malloc(size);
    }
    return profilerMemory.malloc(size);
}

bool MyMalloc::ifInProfilerMemoryThenFree(void * addr) {
    if(threadLocalMemoryInitialized()) {
        if(threadLocalProfilerMemory[ThreadLocalStatus::runningThreadIndex].ifInProfilerMemoryThenFree(addr)) {
            return true;
        }
    }
    for(unsigned int threadIndex = 0; threadIndex < ThreadLocalStatus::totalNumOfThread; ++threadIndex) {
        if(threadIndex != (unsigned int)ThreadLocalStatus::runningThreadIndex && threadLocalMemoryInitialized(threadIndex)) {
            if(threadLocalProfilerMemory[threadIndex].ifInProfilerMemoryThenFree(addr)) {
                return true;
            }
        }
    }
    return profilerMemory.ifInProfilerMemoryThenFree(addr);
}

void * MyMalloc::hashMalloc(size_t size) {
    void * object;
    if(MMAPHashMemoryInitialized()) {
        object = MMAPProfilerHashMemory[ThreadLocalStatus::runningThreadIndex].malloc(size);
    } else {
        object = profilerHashMemory.malloc(size);
    }
    return object;
}
