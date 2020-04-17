//
// Created by 86152 on 2020/2/22.
//
#include <atomic>
#include <stdio.h>
#include "memwaste.h"

#include "privateheap.hh"

HashMap <void*, objStatus, spinlock, PrivateHeap> MemoryWaste::objStatusMap;
thread_local uint64_t* MemoryWaste::mem_alloc_wasted;
thread_local uint64_t* MemoryWaste::mem_alloc_wasted_minus;
thread_local uint64_t* MemoryWaste::mem_freelist_wasted;
thread_local uint64_t* MemoryWaste::mem_freelist_wasted_minus;

bool MemoryWaste::global_init = false;
thread_local bool MemoryWaste::thread_init = false;
///Here
spinlock MemoryWaste::record_lock;
thread_local uint64_t MemoryWaste::now_max_usage = 0;
const uint64_t MemoryWaste::stride;

thread_local uint64_t* MemoryWaste::mem_alloc_wasted_record;
thread_local uint64_t* MemoryWaste::mem_alloc_wasted_record_minus;
thread_local uint64_t* MemoryWaste::mem_freelist_wasted_record;
thread_local uint64_t* MemoryWaste::mem_freelist_wasted_record_minus;

uint64_t * MemoryWaste::mem_alloc_wasted_record_global;
uint64_t * MemoryWaste::mem_alloc_wasted_record_global_minus;
uint64_t * MemoryWaste::mem_freelist_wasted_record_global;
uint64_t * MemoryWaste::mem_freelist_wasted_record_global_minus;

void MemoryWaste::initialize() {
    fprintf(stderr, "memorywaste initialization\n");
    objStatusMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);
    fprintf(stderr, "memorywaste initialization done\n");

    if(bibop) {
        mem_alloc_wasted_record_global = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));
        mem_alloc_wasted_record_global_minus = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));
        mem_freelist_wasted_record_global = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));
        mem_freelist_wasted_record_global_minus = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));

    } else {
        mem_alloc_wasted_record_global = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
        mem_alloc_wasted_record_global_minus = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
        mem_freelist_wasted_record_global = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
        mem_freelist_wasted_record_global_minus = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
    }
    record_lock.init();
    global_init = true;
}

void MemoryWaste::initForNewTid() {

    if(bibop) {
        mem_alloc_wasted = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));
        mem_alloc_wasted_minus = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));
        mem_freelist_wasted = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));
        mem_freelist_wasted_minus = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));
        mem_alloc_wasted_record = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));
        mem_alloc_wasted_record_minus = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));
        mem_freelist_wasted_record = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));
        mem_freelist_wasted_record_minus = (uint64_t*) dlmalloc(num_class_sizes * sizeof(uint64_t));

    } else {
        mem_alloc_wasted = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
        mem_alloc_wasted_minus = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
        mem_freelist_wasted = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
        mem_freelist_wasted_minus = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
        mem_alloc_wasted_record = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
        mem_alloc_wasted_record_minus = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
        mem_freelist_wasted_record = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
        mem_freelist_wasted_record_minus = (uint64_t*) dlmalloc(2 * sizeof(uint64_t));
    }

    thread_init = true;
}


void getClassSizeForStyles(void* uintaddr, allocation_metadata * allocData);

bool MemoryWaste::allocUpdate(allocation_metadata * allocData, void * address) {
    bool reused;
    size_t classSize;
    short classSizeIndex;
    /* New or Reused? Get old status */
    objStatus * status = objStatusMap.find(address, sizeof(unsigned long));;
    if(!status) {
        reused = false;
        /* new status */
        getClassSizeForStyles(address, allocData);
        classSize = allocData->classSize;
        classSizeIndex = allocData->classSizeIndex;
        objStatus newObj; 
        fprintf(stderr, "insert address %p\n", address);
        newObj.size_using = allocData->size;
        newObj.classSize = classSize;
        newObj.classSizeIndex = classSizeIndex;
        status = objStatusMap.insert(address, sizeof(void *), newObj);
    } 
    else {
        reused = true;
        ///Here
        if(status->size_using == allocData->size) {
            allocData->classSize = status->classSize;
            allocData->classSizeIndex = status->classSizeIndex;
        } else {
            getClassSizeForStyles(address, allocData);
        }

        classSize = allocData->classSize;
        classSizeIndex = allocData->classSizeIndex;
///www
        mem_freelist_wasted_minus[status->classSizeIndex] += classSize;

        status->size_using = allocData->size;
        status->classSize = classSize;
        status->classSizeIndex = classSizeIndex;
    }
///www
    if(classSize-allocData->size >= PAGESIZE) {
        mem_alloc_wasted[classSizeIndex] += (classSize/PAGESIZE)*PAGESIZE;
    } else {
        mem_alloc_wasted[classSizeIndex] += classSize;
    }
    mem_alloc_wasted_minus[classSizeIndex] += allocData->size;

    return reused;
}

void MemoryWaste::freeUpdate(void* address) {

    /* Get old status */
    objStatus* status = objStatusMap.find(address, sizeof(void *));
    if (!status) {
        fprintf(stderr, "objStatusMap key error: %p\n", address);
        abort();
    }
    size_t size = status->size_using;
    size_t classSize = status->classSize;
    size_t classSizeIndex = status->classSizeIndex;
///www
    mem_alloc_wasted[classSizeIndex] += size;
    if(classSize-size >= PAGESIZE) {
        mem_alloc_wasted_minus[classSizeIndex] += (classSize/PAGESIZE)*PAGESIZE;
    } else {
        mem_alloc_wasted_minus[classSizeIndex] += classSize;
    }

    mem_freelist_wasted[classSizeIndex] += classSize;

    status->size_using = 0;
    status->classSize = classSize;
//    fprintf(stderr,"free %d, %d, %d, %lu, %lu, %lu, %lu\n", thrData.tid, size, classSizeIndex,
//            mem_alloc_wasted[classSizeIndex], mem_alloc_wasted_minus[classSizeIndex],
//            mem_freelist_wasted[classSizeIndex], mem_freelist_wasted_minus[classSizeIndex]);
}

///Here
bool MemoryWaste::recordMemory(uint64_t now_usage) {

    if(now_usage <= now_max_usage + stride) {
        return false;
    }

    now_max_usage = now_usage;

    if(bibop) {
        memcpy(mem_alloc_wasted_record, mem_alloc_wasted, num_class_sizes * sizeof(uint64_t));
        memcpy(mem_alloc_wasted_record_minus, mem_alloc_wasted_minus, num_class_sizes * sizeof(uint64_t));
        memcpy(mem_freelist_wasted_record, mem_freelist_wasted, num_class_sizes * sizeof(uint64_t));
        memcpy(mem_freelist_wasted_record_minus, mem_freelist_wasted_minus, num_class_sizes * sizeof(uint64_t));

    } else {
        memcpy(mem_alloc_wasted_record, mem_alloc_wasted, 2 * sizeof(uint64_t));
        memcpy(mem_alloc_wasted_record_minus, mem_alloc_wasted_minus, 2 * sizeof(uint64_t));
        memcpy(mem_freelist_wasted_record, mem_freelist_wasted, 2 * sizeof(uint64_t));
        memcpy(mem_freelist_wasted_record_minus, mem_freelist_wasted_minus, 2 * sizeof(uint64_t));
    }



    return true;
}

void MemoryWaste::globalizeMemory() {

    record_lock.lock();

    if(bibop) {
        for (int i = 0; i < num_class_sizes; ++i) {
            mem_alloc_wasted_record_global[i] += mem_alloc_wasted_record[i]/1024/1024;
            mem_alloc_wasted_record_global_minus[i] += mem_alloc_wasted_record_minus[i]/1024/1024;
            mem_freelist_wasted_record_global[i] += mem_freelist_wasted_record[i]/1024/1024;
            mem_freelist_wasted_record_global_minus[i] += mem_freelist_wasted_record_minus[i]/1024/1024;

        }
    } else {
        for (int i = 0; i < 2; ++i) {
            mem_alloc_wasted_record_global[i] += mem_alloc_wasted_record[i]/1024/1024;
            mem_alloc_wasted_record_global_minus[i] += mem_alloc_wasted_record_minus[i]/1024/1024;
            mem_freelist_wasted_record_global[i] += mem_freelist_wasted_record[i]/1024/1024;
            mem_freelist_wasted_record_global_minus[i] += mem_freelist_wasted_record_minus[i]/1024/1024;
        }
    }

    record_lock.unlock();
}
extern size_t * class_sizes;
void MemoryWaste::reportMaxMemory(FILE * output) {

    uint64_t mem_alloc_wasted_record_total = 0;
    uint64_t mem_freelist_wasted_record_total = 0;

    if(output == nullptr) {
        output = stderr;
    }

    fprintf (output, "\n>>>>>>>>>>>>>>>    MEMORY DISTRIBUTION    <<<<<<<<<<<<<<<\n");
    if(bibop) {
        for (int i = 0; i < num_class_sizes; ++i) {
            if(mem_alloc_wasted_record_global[i] < mem_alloc_wasted_record_global_minus[i]) {
                mem_alloc_wasted_record_global[i] = 0;
            } else {
                mem_alloc_wasted_record_global[i] -= mem_alloc_wasted_record_global_minus[i];
            }
            if(mem_freelist_wasted_record_global[i] < mem_freelist_wasted_record_global_minus[i]) {
                mem_freelist_wasted_record_global[i] = 0;
            } else {
                mem_freelist_wasted_record_global[i] -= mem_freelist_wasted_record_global_minus[i];
            }
            fprintf(output, "classsize: %10lu\tmemory in alloc fragments: %10luM\tmemory in freelists: %10luM\n",
                    class_sizes[i], mem_alloc_wasted_record_global[i], mem_freelist_wasted_record_global[i]);
            mem_alloc_wasted_record_total += mem_alloc_wasted_record_global[i];
            mem_freelist_wasted_record_total += mem_freelist_wasted_record_global[i];
        }
    } else {
        for (int i = 0; i < 2; ++i) {
            if(mem_alloc_wasted_record_global[i] < mem_alloc_wasted_record_global_minus[i]) {
                mem_alloc_wasted_record_global[i] = 0;
            } else {
                mem_alloc_wasted_record_global[i] -= mem_alloc_wasted_record_global_minus[i];
            }
            if(mem_freelist_wasted_record_global[i] < mem_freelist_wasted_record_global_minus[i]) {
                mem_freelist_wasted_record_global[i] = 0;
            } else {
                mem_freelist_wasted_record_global[i] -= mem_freelist_wasted_record_global_minus[i];
            }
            if(i == 0) {
                fprintf(output, "small objects\tmemory in alloc fragments: %10luM\tmemory in freelists: %10luM\n",
                        mem_alloc_wasted_record_global[i], mem_freelist_wasted_record_global[i]);
            } else {
                fprintf(output, "large objects\tmemory in alloc fragments: %10luM\tmemory in freelists: %10luM\n",
                        mem_alloc_wasted_record_global[i], mem_freelist_wasted_record_global[i]);
            }
            mem_alloc_wasted_record_total += mem_alloc_wasted_record_global[i];
            mem_freelist_wasted_record_total += mem_freelist_wasted_record_global[i];
        }
    }
    fprintf(output, "total:\t\t\tmemory in alloc fragments: %10luM\tmemory in freelists: %10luM\n",
            mem_alloc_wasted_record_total, mem_freelist_wasted_record_total);

}

#if 1
size_t MemoryWaste::getSize(void * address) {
    objStatus * status = objStatusMap.find(address, sizeof(void *));
    if(status) {
        return status->size_using;
    } else {
        return 0;
    }
}

size_t MemoryWaste::getClassSize(void * address) {
    objStatus * status = objStatusMap.find(address, sizeof(void *));
    if(status) {
        return status->classSize;
    } else {
        return 0;
    }
}
#endif

void MemoryWaste::checkGlobalInit() {
    if(!global_init) {
        initialize();
    }
}

void MemoryWaste::checkThreadInit() {
    if(!thread_init) {
        initForNewTid();
    }
}
