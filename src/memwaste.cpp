//
// Created by 86152 on 2020/2/22.
//
#include <atomic>
#include <stdio.h>
#include "memwaste.h"

HashMap <void*, objStatus, spinlock, PrivateHeap> MemoryWaste::objStatusMap;
thread_local uint64_t* MemoryWaste::mem_alloc_wasted;
thread_local uint64_t* MemoryWaste::mem_alloc_wasted_minus;
thread_local uint64_t* MemoryWaste::mem_freelist_wasted;
thread_local uint64_t* MemoryWaste::mem_freelist_wasted_minus;

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

thread_local uint64_t * MemoryWaste::mem_blowup;
thread_local uint64_t * MemoryWaste::mem_blowup_minus;
thread_local uint64_t * MemoryWaste::mem_blowup_record;
thread_local uint64_t * MemoryWaste::mem_blowup_minus_record;
uint64_t * MemoryWaste::mem_blowup_global;
uint64_t * MemoryWaste::mem_blowup_global_minus;
int64_t * MemoryWaste::free_nums;

thread_local uint64_t * MemoryWaste::num_alloc;
thread_local uint64_t * MemoryWaste::num_allocFFL;
thread_local uint64_t * MemoryWaste::num_free;

uint64_t * MemoryWaste::num_alloc_global;
uint64_t * MemoryWaste::num_allocFFL_global;
uint64_t * MemoryWaste::num_free_global;

thread_local uint64_t * MemoryWaste::num_alloc_record;
thread_local uint64_t * MemoryWaste::num_allocFFL_record;
thread_local uint64_t * MemoryWaste::num_free_record;

uint64_t * MemoryWaste::num_alloc_record_global;
uint64_t * MemoryWaste::num_allocFFL_record_global;
uint64_t * MemoryWaste::num_free_record_global;

void MemoryWaste::initialize() {
    objStatusMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);

    mem_alloc_wasted_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_alloc_wasted_record_global_minus = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_freelist_wasted_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_freelist_wasted_record_global_minus = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

    mem_blowup_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_blowup_global_minus = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    free_nums = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));

    num_alloc_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_allocFFL_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_free_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

    num_alloc_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_allocFFL_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_free_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

    record_lock.init();
}

void MemoryWaste::initForNewTid() {

    mem_alloc_wasted = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_alloc_wasted_minus = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_freelist_wasted = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_freelist_wasted_minus = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_alloc_wasted_record = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_alloc_wasted_record_minus = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_freelist_wasted_record = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_freelist_wasted_record_minus = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

    mem_blowup = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_blowup_minus = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_blowup_record = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_blowup_minus_record = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

    num_alloc = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_allocFFL = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_free = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

    num_alloc_record = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_allocFFL_record = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_free_record = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
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
        num_alloc[classSizeIndex]++;
        objStatus newObj; 
        //fprintf(stderr, "insert address %p\n", address);
        newObj.size_using = allocData->size;
        newObj.classSize = classSize;
        newObj.classSizeIndex = classSizeIndex;
        if(free_nums[classSizeIndex] > 0) {
            mem_blowup[classSizeIndex] += allocData->size;
            newObj.inBlowup = true;
        } else {
            newObj.inBlowup = false;
        }
        status = objStatusMap.insert(address, sizeof(void *), newObj);
    }
    else {
        reused = true;

        free_nums[status->classSizeIndex]--;

        if(status->size_using == allocData->size) {
            allocData->classSize = status->classSize;
            allocData->classSizeIndex = status->classSizeIndex;
        } else {
            getClassSizeForStyles(address, allocData);
        }

        classSize = allocData->classSize;
        classSizeIndex = allocData->classSizeIndex;
        num_allocFFL[classSizeIndex]++;
        mem_freelist_wasted_minus[status->classSizeIndex] += classSize;

        status->size_using = allocData->size;
        status->classSize = classSize;
        status->classSizeIndex = classSizeIndex;

    }

    if(classSize-allocData->size >= PAGESIZE) {
        mem_alloc_wasted[classSizeIndex] += (classSize/PAGESIZE)*PAGESIZE;
    } else {
        mem_alloc_wasted[classSizeIndex] += classSize;
    }
    mem_alloc_wasted_minus[classSizeIndex] += allocData->size;


    return reused;
}

void MemoryWaste::freeUpdate(allocation_metadata * allocData, void* address) {

    /* Get old status */
    objStatus* status = objStatusMap.find(address, sizeof(void *));
    if (!status) {
        fprintf(stderr, "objStatusMap key error: %p\n", address);
        abort();
    }
    size_t size = status->size_using;
    size_t classSize = status->classSize;
    size_t classSizeIndex = status->classSizeIndex;

    num_free[classSizeIndex]++;

    if(status->inBlowup) {
        mem_blowup_minus[classSizeIndex] += allocData->size;
    }

    allocData->size = size;
    allocData->classSize = classSize;

    mem_alloc_wasted[classSizeIndex] += size;
    if(classSize-size >= PAGESIZE) {
        mem_alloc_wasted_minus[classSizeIndex] += (classSize/PAGESIZE)*PAGESIZE;
    } else {
        mem_alloc_wasted_minus[classSizeIndex] += classSize;
    }

    mem_freelist_wasted[classSizeIndex] += classSize;

    status->size_using = 0;
    status->classSize = classSize;

    free_nums[classSizeIndex]++;
}


bool MemoryWaste::recordMemory(uint64_t now_usage) {

    if(now_usage <= now_max_usage + stride) {
        return false;
    }

    now_max_usage = now_usage;

    memcpy(mem_alloc_wasted_record, mem_alloc_wasted, num_class_sizes * sizeof(uint64_t));
    memcpy(mem_alloc_wasted_record_minus, mem_alloc_wasted_minus, num_class_sizes * sizeof(uint64_t));
    memcpy(mem_freelist_wasted_record, mem_freelist_wasted, num_class_sizes * sizeof(uint64_t));
    memcpy(mem_freelist_wasted_record_minus, mem_freelist_wasted_minus, num_class_sizes * sizeof(uint64_t));

    memcpy(mem_blowup_record, mem_blowup, num_class_sizes * sizeof(uint64_t));
    memcpy(mem_blowup_minus_record, mem_blowup_minus, num_class_sizes * sizeof(uint64_t));

    memcpy(num_alloc_record, num_alloc, num_class_sizes * sizeof(uint64_t));
    memcpy(num_allocFFL_record, num_allocFFL, num_class_sizes * sizeof(uint64_t));
    memcpy(num_free_record, num_free, num_class_sizes * sizeof(uint64_t));

    return true;
}

void MemoryWaste::globalizeMemory() {

    record_lock.lock();
    for (int i = 0; i < num_class_sizes; ++i) {
        mem_alloc_wasted_record_global[i] += mem_alloc_wasted_record[i];
        mem_alloc_wasted_record_global_minus[i] += mem_alloc_wasted_record_minus[i];
        mem_freelist_wasted_record_global[i] += mem_freelist_wasted_record[i];
        mem_freelist_wasted_record_global_minus[i] += mem_freelist_wasted_record_minus[i];

        mem_blowup_global[i] += mem_blowup_record[i];
        mem_blowup_global_minus[i] += mem_blowup_minus_record[i];

        num_alloc_global[i] += num_alloc[i];
        num_allocFFL_global[i] += num_allocFFL[i];
        num_free_global[i] += num_free[i];

        num_alloc_record_global[i] += num_alloc_record[i];
        num_allocFFL_record_global[i] += num_allocFFL_record[i];
        num_free_record_global[i] += num_free_record[i];
    }

    record_lock.unlock();
}
extern size_t * class_sizes;

void MemoryWaste::reportAllocDistribution(FILE * output) {
    fprintf (output, ">>>>>>>>>>>>>>>    ALLOCATION DISTRIBUTION    <<<<<<<<<<<<<<<\n");
    for (int i = 0; i < num_class_sizes; ++i) {
        if(bibop) {
            fprintf(output, "classsize: %10lu\t\t\t", class_sizes[i]);
        } else {
            if(i == 0) {
                fprintf(output, "small object:\t\t\t\t\t\t\t\t\t\t");
            } else {
                fprintf(output, "large object:\t\t\t\t\t\t\t\t\t\t");
            }
        }
        fprintf(output, "new alloc: %10lu\t\t\tfreelist alloc: %10lu\t\t\tfree: %10lu\t\t\t",
                num_alloc_global[i], num_allocFFL_global[i], num_free_global[i]);
        uint64_t leak = 0;
        if(num_alloc_global[i] + num_allocFFL_global[i] > num_free_global[i]) {
            leak = num_alloc_global[i] + num_allocFFL_global[i] - num_free_global[i];
        } else {
            leak = 0;
        }
        fprintf(output, "potential leak num: %10lu\n", leak);
    }
}

void MemoryWaste::reportMaxMemory(FILE * output) {

    uint64_t mem_alloc_wasted_record_total = 0;
    uint64_t mem_freelist_wasted_record_total = 0;
    uint64_t mem_blowup_total = 0;
    uint64_t num_alloc_total = 0;
    uint64_t num_allocFFL_total = 0;
    uint64_t num_free_total = 0;

    if(output == nullptr) {
        output = stderr;
    }

    fprintf (output, "\n>>>>>>>>>>>>>>>    MEMORY DISTRIBUTION    <<<<<<<<<<<<<<<\n");
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
        if(mem_blowup_global[i] < mem_blowup_minus[i]) {
            mem_blowup_global[i] = 0;
        }
        if(bibop) {
            fprintf(output, "classsize: %10lu\t\t\t", class_sizes[i]);
        } else {
            if(i == 0) {
                fprintf(output, "small object:\t\t\t\t\t\t\t");
            } else {
                fprintf(output, "large object:\t\t\t\t\t\t\t");
            }
        }
        fprintf(output, "memory in alloc fragments: %10luM\t\t\tmemory in freelists: %10luM\t\t\t"
                        "memory blowup: %10luM\t\t\t"
                        "new alloc: %10lu\t\t\tfreelist alloc: %10lu\t\t\tfree: %10lu\n",
                mem_alloc_wasted_record_global[i]/1024/1024, mem_freelist_wasted_record_global[i]/1024/1024, mem_blowup_global[i]/1024/1024,
                num_alloc_record_global[i], num_allocFFL_record_global[i], num_free_record_global[i]);

        mem_alloc_wasted_record_total += mem_alloc_wasted_record_global[i];
        mem_freelist_wasted_record_total += mem_freelist_wasted_record_global[i];
        mem_blowup_total += mem_blowup_global[i];

        num_alloc_total += num_alloc_record_global[i];
        num_allocFFL_total += num_allocFFL_record_global[i];
        num_free_total += num_free_record_global[i];
    }

    fprintf(output, "total:\t\t\t\t\t\t\t\t\t\tmemory in alloc fragments: %10luM\t\t\tmemory in freelists: %10luM\t\t\t"
                    "memory blowup: %10luM\t\t\t"
                    "new alloc: %10lu\t\t\tfreelist alloc: %10lu\t\t\tfree: %10lu\n",
            mem_alloc_wasted_record_total/1024/1024, mem_freelist_wasted_record_total/1024/1024, mem_blowup_total/1024/1024,
            num_alloc_total, num_allocFFL_total, num_free_total);

}

