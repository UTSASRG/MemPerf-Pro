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

thread_local int64_t * MemoryWaste::num_alloc_active;
thread_local int64_t * MemoryWaste::num_alloc_active_record;
int64_t * MemoryWaste::num_alloc_active_global;
thread_local int64_t * MemoryWaste::num_freelist;
thread_local int64_t * MemoryWaste::num_freelist_record;
int64_t * MemoryWaste::num_freelist_global;

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

int64_t * MemoryWaste::blowupflag_record;
int64_t * MemoryWaste::blowupflag;

void MemoryWaste::initialize() {
    objStatusMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);

    mem_alloc_wasted_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_alloc_wasted_record_global_minus = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_freelist_wasted_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    mem_freelist_wasted_record_global_minus = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

    num_alloc_active_global = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));
    num_freelist_global = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));

    num_alloc_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_allocFFL_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_free_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

    num_alloc_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_allocFFL_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_free_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

    blowupflag_record = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));
    blowupflag = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));

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

    num_alloc_active = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));
    num_alloc_active_record = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));
    num_freelist = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));
    num_freelist_record = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));

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
        status = objStatusMap.insert(address, sizeof(void *), newObj);
    }
    else {
        reused = true;
        if(status->size_using == allocData->size) {
            allocData->classSize = status->classSize;
            allocData->classSizeIndex = status->classSizeIndex;
        } else {
            getClassSizeForStyles(address, allocData);
        }

        classSize = allocData->classSize;
        classSizeIndex = allocData->classSizeIndex;

        num_allocFFL[classSizeIndex]++;
        num_freelist[classSizeIndex]--;

        mem_freelist_wasted_minus[status->classSizeIndex] += classSize;

        status->size_using = allocData->size;
        status->classSize = classSize;
        status->classSizeIndex = classSizeIndex;

    }

    num_alloc_active[classSizeIndex]++;

    if(classSize-allocData->size >= PAGESIZE) {
        //mem_alloc_wasted[classSizeIndex] += (classSize/PAGESIZE)*PAGESIZE;
        mem_alloc_wasted[classSizeIndex] += classSize;
        mem_alloc_wasted_minus[classSizeIndex] += (classSize-allocData->size)/PAGESIZE*PAGESIZE;
    } else {
        mem_alloc_wasted[classSizeIndex] += classSize;
    }
    mem_alloc_wasted_minus[classSizeIndex] += allocData->size;

    if(blowupflag[classSizeIndex] > 0) {
        blowupflag[classSizeIndex]--;
    }


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
    if(size == 0) {
        return;
    }
    num_free[classSizeIndex]++;
    num_freelist[classSizeIndex]++;
    num_alloc_active[classSizeIndex]--;

    allocData->size = size;
    allocData->classSize = classSize;

    mem_alloc_wasted[classSizeIndex] += size;
    if(classSize-size >= PAGESIZE) {
        //mem_alloc_wasted_minus[classSizeIndex] += (classSize/PAGESIZE)*PAGESIZE;
        mem_alloc_wasted_minus[classSizeIndex] += classSize;
        mem_alloc_wasted[classSizeIndex] += (classSize-allocData->size)/PAGESIZE*PAGESIZE;
    } else {
        mem_alloc_wasted_minus[classSizeIndex] += classSize;
    }

    mem_freelist_wasted[classSizeIndex] += classSize;

    status->size_using = 0;
    status->classSize = classSize;

    blowupflag[classSizeIndex]++;

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

    memcpy(num_alloc_record, num_alloc, num_class_sizes * sizeof(uint64_t));
    memcpy(num_allocFFL_record, num_allocFFL, num_class_sizes * sizeof(uint64_t));
    memcpy(num_free_record, num_free, num_class_sizes * sizeof(uint64_t));

    memcpy(num_alloc_active_record, num_alloc_active, num_class_sizes * sizeof(int64_t));
    memcpy(num_freelist_record, num_freelist, num_class_sizes * sizeof(int64_t));

    memcpy(blowupflag_record, blowupflag, num_class_sizes * sizeof(int64_t));

    return true;
}

void MemoryWaste::globalizeMemory() {

    record_lock.lock();
    for (int i = 0; i < num_class_sizes; ++i) {
        mem_alloc_wasted_record_global[i] += mem_alloc_wasted_record[i];
        mem_alloc_wasted_record_global_minus[i] += mem_alloc_wasted_record_minus[i];
        mem_freelist_wasted_record_global[i] += mem_freelist_wasted_record[i];
        mem_freelist_wasted_record_global_minus[i] += mem_freelist_wasted_record_minus[i];

        num_alloc_global[i] += num_alloc[i];
        num_allocFFL_global[i] += num_allocFFL[i];
        num_free_global[i] += num_free[i];

        num_alloc_record_global[i] += num_alloc_record[i];
        num_allocFFL_record_global[i] += num_allocFFL_record[i];
        num_free_record_global[i] += num_free_record[i];

        num_alloc_active_global[i] += num_alloc_active_record[i];
        num_freelist_global[i] += num_freelist_record[i];
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

void MemoryWaste::reportMaxMemory(FILE * output, long realMem, long totalMem) {

    uint64_t mem_alloc_wasted_record_total = 0;
    uint64_t mem_freelist_wasted_record_total = 0;
    uint64_t mem_blowup_total = 0;
    uint64_t num_alloc_active_total = 0;
    uint64_t num_freelist_total = 0;
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
        if(num_alloc_active_global[i] < 0) {
            num_alloc_active_global[i] = 0;
        }
        if(num_freelist_global[i] < 0) {
            num_freelist_global[i] = 0;
        }
        if(blowupflag_record[i] < 0) {
            blowupflag_record[i] = 0;
        }

        int64_t blowup = class_sizes[i] * (num_freelist_global[i] - blowupflag_record[i]);
        if(blowup < 0) {
            blowup = 0;
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
        fprintf(output, "memory in alloc fragments: %10luK\t\t\tmemory in freelists: %10luK\t\t\t"
                        "memory blowup: %10luK\t\t\t"
                        "active alloc: %10ld\t\t\tfreelist objects: %10ld\t\t\tfree: %10lu\n",
                mem_alloc_wasted_record_global[i]/1024, mem_freelist_wasted_record_global[i]/1024, blowup/1024,
                num_alloc_active_global[i], num_freelist_global[i], num_free_record_global[i]);

        mem_alloc_wasted_record_total += mem_alloc_wasted_record_global[i];
        mem_freelist_wasted_record_total += mem_freelist_wasted_record_global[i];
        mem_blowup_total += blowup;

        num_alloc_active_total += num_alloc_active_global[i];
        num_freelist_total += num_freelist_global[i];
        num_free_total += num_free_record_global[i];
    }

    int64_t exfrag = totalMem - realMem - mem_alloc_wasted_record_total - mem_blowup_total;
    if(exfrag < 0) {
        exfrag = 0;
    }

    fprintf(output, "\ntotal:\t\t\t\t\t\t\t\t\t\t"
                    "memory in alloc fragments:%10luK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                    "memory in freelists:\t\t\t%10luK\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                    "memory blowup:\t\t\t\t\t\t%10ldK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                    "external fragment:\t\t\t\t%10ldK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                    "real using memory:\t\t\t\t%10ldK(%3d%%)\n"
                    "\ntotal:\t\t\t\t\t\t\t\t\t\tactive alloc: %10lu\t\t\tfreelist objects: %10lu\t\t\tfree: %10lu\n",
            mem_alloc_wasted_record_total/1024, mem_alloc_wasted_record_total*100/totalMem,
            mem_freelist_wasted_record_total/1024,
            mem_blowup_total/1024, mem_blowup_total*100/totalMem, exfrag/1024, exfrag*100/totalMem,
            realMem/1024, realMem*100/totalMem,
            num_alloc_active_total, num_freelist_total, num_free_total);

}

