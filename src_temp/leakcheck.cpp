/*
 * @file   leakcheck.cpp
 * @brief  Detecting leakage usage case.
           Basic idea:
           We first traverse the heap to get an alive list (not freed objects) and verify whether
           these objects are reachable from stack, registers and global variables or not.
           If an object is not freed and it is not reachable, then it is a memory leak.

           In order to verify whether an object is reachable, we start from the root list (those
           possible reachable objects).

           However, it is much easier for the checking in the end of a program. We basically think
           that every object should be freed. Thus, we only needs to search the heap list to find
           those unfreed objects. If there is someone, then we reported that and rollback.

           In order to detect those callsites for those memory leakage, we basically maintain
           a hashtable. Whenever there is memory allocation, we check whether this object is a
           possible memory leakage. If yes, then we update corresponding list about how many leakage
           happens on each memory allocation site.

 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include "leakcheck.hh"

#ifdef PRINT_LEAK_OBJECTS

thread_local std::list<unsigned long> leakcheck::_unexploredObjects;
size_t leakcheck::_totalLeakageSize;
unsigned long leakcheck::_heapBegin;
unsigned long leakcheck::_heapEnd;

extern HashMap<void*, ObjectStatus, PrivateHeap> objStatusMap;

void leakcheck::doSlowLeakCheck(unsigned long begin, unsigned long end) {
    _heapBegin = begin;
    _heapEnd = end;
    ucontext_t context;
    getcontext(&context);
//    fprintf(stderr, "heap pointers\n");
    searchHeapPointers(&context);
//    fprintf(stderr, "stack\n");
//fprintf(stderr, "thread stack from %p to %p\n", ThreadLocalStatus::stackBottom, &context);
    searchHeapPointersInsideStack(&context);
//    mark();
//    sweep();
}

void leakcheck::exploreHeapObject(unsigned long addr) {
//    fprintf(stderr, "to get lock\n");
//    MemoryWaste::hashLocksSet.lock((void*)addr);
//    fprintf(stderr, "get lock\n");
    ObjectStatus* object = objStatusMap.find((void*)addr, sizeof(void*));
    if(object && !object->mark && object->allocated) {
        unsigned long end = addr + object->sizeClassSizeAndIndex.size;
        object->mark = true;
//        MemoryWaste::hashLocksSet.unlock((void*)addr);
        searchHeapPointers((unsigned long)addr, (unsigned long)end);
    }
    else {
//        MemoryWaste::hashLocksSet.unlock((void*)addr);
    }

}

void leakcheck::mark() {
    while(!_unexploredObjects.empty()) {
        unsigned long addr = _unexploredObjects.front();
        _unexploredObjects.pop_front();
        exploreHeapObject(addr);
//        fprintf(stderr, "here\n");
    }
}

void leakcheck::sweep() {
//    fprintf(stderr, "globals\n");
    selfmap::getInstance().getGlobalRegions();
    searchHeapPointersInsideGlobals();
//    fprintf(stderr, "Going to mark...\n");
    mark();
//    fprintf(stderr, "Mark done. Do sweep...\n");
    _totalLeakageSize = 0;
//    for(unsigned int i = 0; i < MAX_OBJ_NUM; ++i) {
//        MemoryWaste::hashLocksSet.locks[i].lock();
//    }

    for(auto entryInHashTable: objStatusMap) {
//        if(MemoryWaste::hashLocksSet.locks[(uint64_t)entryInHashTable.getKey()]._lock) {
//            continue;
//        }
//        MemoryWaste::hashLocksSet.lock((void*)entryInHashTable.getKey());
        ObjectStatus* object = entryInHashTable.getValue();
//        fprintf(stderr, "%lu: %p, sz = %u\n", ThreadLocalStatus::runningThreadIndex, entryInHashTable.getKey(), object->sizeClassSizeAndIndex.size);
        if(object->mark) {
//            fprintf(stderr, "marked\n");
            object->mark = false;
        } else if (object->allocated && object->tid == ThreadLocalStatus::runningThreadIndex && entryInHashTable.getKey() != AllocatingStatus::allocatingType.objectAddress) {

                //            fprintf(stderr, "leaked\n");
                _totalLeakageSize += object->sizeClassSizeAndIndex.size;
                Backtrace::addLeak(object->callKey, object->sizeClassSizeAndIndex.size);
//          fprintf(stderr, "%lu free: %p, sz = %u\n", ThreadLocalStatus::runningThreadIndex, entryInHashTable.getKey(), object->sizeClassSizeAndIndex.size);
//          RealX::free(entryInHashTable.getKey());
        }
//        MemoryWaste::hashLocksSet.unlock((void*)entryInHashTable.getKey());
    }
//    for(unsigned int i = 0; i < MAX_OBJ_NUM; ++i) {
//        MemoryWaste::hashLocksSet.locks[i].unlock();
//    }
//    fprintf(stderr, "Sweep done...\n");
}

void leakcheck::searchHeapPointersInsideGlobals() {
    for(uint8_t i = 0; i < numOfRegion; ++i) {
        regioninfo r = regions[i];
        searchHeapPointers((unsigned long)r.start, (unsigned long)r.end);
    }
    for(uint8_t i = 1; i < 2; ++i) {
        regioninfo r = silentRegions[i];
//        fprintf(stderr, "search silent heap %u\n", i);
        searchHeapPointers((unsigned long)r.start, (unsigned long)r.end);
    }
}

bool leakcheck::isPossibleHeapPointer(unsigned long addr) {
    if (addr > _heapBegin && addr < _heapEnd) {
        if(ObjectStatus * status = objStatusMap.find((void*)addr, sizeof(void*))) {
            if(!status->mark)
            return true;
        }
    }
    return false;
}

void leakcheck::checkInsertUnexploredList(unsigned long addr) {
    if(isPossibleHeapPointer(addr)) {
        _unexploredObjects.push_back(addr);
    }
}

void leakcheck::searchHeapPointers(unsigned long start, unsigned long end) {
    assert(((intptr_t)start) % sizeof(unsigned long) == 0);
    unsigned long* stop = (unsigned long*)aligndown(end, sizeof(void*));
    unsigned long* ptr = (unsigned long*)start;
//    fprintf(stderr, "search from %p to %p\n", (void *)start, (void *)end);
    while(ptr < stop) {
        checkInsertUnexploredList(*ptr);
        ptr++;
    }
}

// Search heap pointers inside registers set.
void leakcheck::searchHeapPointers(ucontext_t* context) {
    for(int i = REG_R8; i <= REG_RCX; i++) {
        checkInsertUnexploredList(context->uc_mcontext.gregs[i]);
    }
}

void leakcheck::searchHeapPointersInsideStack(void* start) {
    void* stop = ThreadLocalStatus::stackBottom;
//    fprintf(stderr, "%lu check stack range: %p - %p\n", ThreadLocalStatus::runningThreadIndex, start, stop);
    searchHeapPointers((unsigned long)start, (unsigned long)stop);
}

void leakcheck::debugPrintQueue() {
    while(!_unexploredObjects.empty()) {
        unsigned long addr = _unexploredObjects.front();
        _unexploredObjects.pop_front();
        fprintf(stderr, "catch %lu\n", addr);
    }
}

void leakcheck::handler(int signo) {
    fprintf(stderr, "%d receive signal %d...checking memory leaking\n", ThreadLocalStatus::runningThreadIndex, signo);

    if(MemoryWaste::minAddr != (uint64_t)-1 && MemoryWaste::maxAddr) {
        leakcheck::doSlowLeakCheck(MemoryWaste::minAddr, MemoryWaste::maxAddr);
        leakcheck::sweep();
        fprintf(stderr, "leak = %luKb\n", leakcheck::_totalLeakageSize / ONE_KB);
    } else {
        fprintf(stderr, "leak = 0Kb\n");
    }
}

#endif