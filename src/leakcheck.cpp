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
    searchHeapPointers(&context);
    searchHeapPointersInsideStack(&context);
    mark();
//    sweep();
}

void leakcheck::exploreHeapObject(unsigned long addr) {
    MemoryWaste::hashLocksSet.lock((void*)addr);
    ObjectStatus* object = objStatusMap.find((void*)addr, sizeof(void*));
    if(object && !object->mark && object->allocated) {
        unsigned long end = addr + object->sizeClassSizeAndIndex.size;
        searchHeapPointers((unsigned long)addr, (unsigned long)end);
        object->mark = true;
    }
    MemoryWaste::hashLocksSet.unlock((void*)addr);
}

void leakcheck::mark() {
    while(!_unexploredObjects.empty()) {
        unsigned long addr = _unexploredObjects.front();
        _unexploredObjects.pop_front();
        exploreHeapObject(addr);
    }
}

void leakcheck::sweep() {
    searchHeapPointersInsideGlobals();
    mark();
    _totalLeakageSize = 0;
//    for(unsigned int i = 0; i < MAX_OBJ_NUM; ++i) {
//        MemoryWaste::hashLocksSet.locks[i].lock();
//    }

    for(auto entryInHashTable: objStatusMap) {
        MemoryWaste::hashLocksSet.lock((void*)entryInHashTable.getKey());
        ObjectStatus* object = entryInHashTable.getValue();
        if(object->mark) {
//            fprintf(stderr, "marked\n");
            object->mark = false;
        } else if (object->allocated && object->tid == ThreadLocalStatus::runningThreadIndex) {
//            fprintf(stderr, "leaked\n");
            _totalLeakageSize += object->sizeClassSizeAndIndex.size;
//          fprintf(stderr, "%lu free: %p\n", ThreadLocalStatus::runningThreadIndex, entryInHashTable.getKey());
//          RealX::free(entryInHashTable.getKey());
        } else {
//            fprintf(stderr, "freed\n");
        }
        MemoryWaste::hashLocksSet.unlock((void*)entryInHashTable.getKey());
    }
//    for(unsigned int i = 0; i < MAX_OBJ_NUM; ++i) {
//        MemoryWaste::hashLocksSet.locks[i].unlock();
//    }

}

void leakcheck::searchHeapPointersInsideGlobals() {
    for(uint8_t i = 0; i < numOfRegion; ++i) {
        regioninfo r = regions[i];
        searchHeapPointers((unsigned long)r.start, (unsigned long)r.end);
    }
}

bool leakcheck::isPossibleHeapPointer(unsigned long addr) {
    if (addr > _heapBegin && addr < _heapEnd) {
        if(objStatusMap.find((void*)addr, sizeof(void*))) {
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
    void* stop = ThreadLocalStatus::stackStartAddress;
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
