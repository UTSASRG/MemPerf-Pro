//
// Created by 86152 on 2020/2/22.
//
//#include <atomic>
#include <stdio.h>
#include "memwaste.h"

HashMap <void*, objStatus, spinlock, PrivateHeap> MemoryWaste::objStatusMap;
uint64_t* MemoryWaste::mem_alloc_wasted;
uint64_t* MemoryWaste::mem_alloc_wasted_minus;
uint64_t* MemoryWaste::mem_freelist_wasted;
uint64_t* MemoryWaste::mem_freelist_wasted_minus;

spinlock MemoryWaste::record_lock;
uint64_t MemoryWaste::now_max_usage = 0;
const uint64_t MemoryWaste::stride;

uint64_t* MemoryWaste::mem_alloc_wasted_record;
uint64_t* MemoryWaste::mem_alloc_wasted_record_minus;
uint64_t* MemoryWaste::mem_freelist_wasted_record;
uint64_t* MemoryWaste::mem_freelist_wasted_record_minus;

int64_t * MemoryWaste::num_alloc_active;
int64_t * MemoryWaste::num_alloc_active_record;
int64_t * MemoryWaste::num_freelist;
int64_t * MemoryWaste::num_freelist_record;

uint64_t * MemoryWaste::num_alloc;
uint64_t * MemoryWaste::num_allocFFL;
uint64_t * MemoryWaste::num_free;

uint64_t * MemoryWaste::num_alloc_record;
uint64_t * MemoryWaste::num_allocFFL_record;
uint64_t * MemoryWaste::num_free_record;

int64_t * MemoryWaste::blowupflag_record;
int64_t * MemoryWaste::blowupflag;

void MemoryWaste::initialize() {
    objStatusMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);

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

    blowupflag_record = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));
    blowupflag = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));

    record_lock.init();
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

        __atomic_add_fetch(&num_alloc[classSizeIndex], 1, __ATOMIC_RELAXED);

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

        __atomic_add_fetch(&num_allocFFL[classSizeIndex], 1, __ATOMIC_RELAXED);
        __atomic_sub_fetch(&num_freelist[classSizeIndex], 1, __ATOMIC_RELAXED);

        __atomic_add_fetch(&mem_freelist_wasted_minus[status->classSizeIndex], classSize, __ATOMIC_RELAXED);

        status->size_using = allocData->size;
        status->classSize = classSize;
        status->classSizeIndex = classSizeIndex;

    }

    __atomic_add_fetch(&num_alloc_active[classSizeIndex], 1, __ATOMIC_RELAXED);

    __atomic_add_fetch(&mem_alloc_wasted[classSizeIndex], classSize, __ATOMIC_RELAXED);
    if(classSize-allocData->size >= PAGESIZE) {
        __atomic_add_fetch(&mem_alloc_wasted_minus[classSizeIndex], (classSize-allocData->size)/PAGESIZE*PAGESIZE, __ATOMIC_RELAXED);
    }
    __atomic_add_fetch(&mem_alloc_wasted_minus[classSizeIndex], allocData->size, __ATOMIC_RELAXED);

    if(__atomic_load_n(&blowupflag[classSizeIndex], __ATOMIC_RELAXED) > 0) {
        __atomic_sub_fetch(&blowupflag[classSizeIndex], 1, __ATOMIC_RELAXED);
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
    __atomic_add_fetch(&num_free[classSizeIndex], 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&num_freelist[classSizeIndex], 1, __ATOMIC_RELAXED);
    __atomic_sub_fetch(&num_alloc_active[classSizeIndex], 1, __ATOMIC_RELAXED);

    allocData->size = size;
    allocData->classSize = classSize;

    __atomic_add_fetch(&mem_alloc_wasted[classSizeIndex], size, __ATOMIC_RELAXED);
    __atomic_add_fetch(&mem_alloc_wasted_minus[classSizeIndex], classSize, __ATOMIC_RELAXED);
    if(classSize-size >= PAGESIZE) {
        __atomic_add_fetch(&mem_alloc_wasted[classSizeIndex], (classSize-allocData->size)/PAGESIZE*PAGESIZE, __ATOMIC_RELAXED);
    }

    __atomic_add_fetch(&mem_freelist_wasted[classSizeIndex], classSize, __ATOMIC_RELAXED);

    status->size_using = 0;
    status->classSize = classSize;

    __atomic_add_fetch(&blowupflag[classSizeIndex], 1, __ATOMIC_RELAXED);

}


bool MemoryWaste::recordMemory(uint64_t now_usage) {

    if(now_usage <= __atomic_load_n(&now_max_usage, __ATOMIC_RELAXED) + stride) {
        return false;
    }
    __atomic_store_n(&now_max_usage, now_usage, __ATOMIC_SEQ_CST);

    record_lock.lock();

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

    record_lock.unlock();

    return true;
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
                num_alloc[i], num_allocFFL[i], num_free[i]);
        uint64_t leak = 0;
        if(num_alloc[i] + num_allocFFL[i] > num_free[i]) {
            leak = num_alloc[i] + num_allocFFL[i] - num_free[i];
        } else {
            leak = 0;
        }
        fprintf(output, "potential leak num: %10lu\n", leak);
    }
}

void MemoryWaste::reportMaxMemory(FILE * output, long realMem, long totalMem) {

    uint64_t mem_alloc_wasted_record_total = 0;
    uint64_t mem_blowup_total = 0;

    uint64_t num_alloc_active_total = 0;
    uint64_t num_freelist_total = 0;

    uint64_t num_alloc_total = 0;
    uint64_t num_allocFFL_total;
    uint64_t num_free_total = 0;

    if(output == nullptr) {
        output = stderr;
    }

    fprintf (output, "\n>>>>>>>>>>>>>>>    MEMORY DISTRIBUTION    <<<<<<<<<<<<<<<\n");
    for (int i = 0; i < num_class_sizes; ++i) {

        if(mem_alloc_wasted_record[i] < mem_alloc_wasted_record_minus[i]) {
            mem_alloc_wasted_record[i] = 0;
        } else {
            mem_alloc_wasted_record[i] -= mem_alloc_wasted_record_minus[i];
        }
        if(num_alloc_active_record[i] < 0) {
            num_alloc_active_record[i] = 0;
        }
        if(num_freelist_record[i] < 0) {
            num_freelist_record[i] = 0;
        }
        if(blowupflag_record[i] < 0) {
            blowupflag_record[i] = 0;
        }

        int64_t blowup = class_sizes[i] * (num_freelist_record[i] - blowupflag_record[i]);
        //int64_t blowup = class_sizes[i] * (num_alloc_record_global[i] - blowupflag_record[i]);
        if(blowup < 0) {
            blowup = 0;
        }

        if(bibop) {
            fprintf(output, "size: %10lu\t\t\t", class_sizes[i]);
        } else {
            if(i == 0) {
                fprintf(output, "small object:\t\t\t\t\t\t\t");
            } else {
                fprintf(output, "large object:\t\t\t\t\t\t\t");
            }
        }
        fprintf(output, "internal fragments: %10luK\t\t\tmemory blowup: %10luK\t\t\t"
                        "active alloc: %10ld\t\t\tfreelist objects: %10ld\t\t\t"
                        "new allocated: %10lu\t\t\treused allocated: %10lu\t\t\tfreed: %10lu\n",
                mem_alloc_wasted_record[i]/1024, blowup/1024,
                num_alloc_active_record[i], num_freelist_record[i],
                num_alloc_record[i], num_allocFFL_record[i], num_free_record[i]);

        mem_alloc_wasted_record_total += mem_alloc_wasted_record[i];
        mem_blowup_total += blowup;

        num_alloc_active_total += num_alloc_active_record[i];
        num_freelist_total += num_freelist_record[i];
        num_alloc_total += num_alloc_record[i];
        num_allocFFL_total += num_allocFFL_record[i];
        num_free_total += num_free_record[i];
    }

    int64_t exfrag = totalMem - realMem - mem_alloc_wasted_record_total - mem_blowup_total;
    if(exfrag < 0) {
        exfrag = 0;
    }

    fprintf(output, "\ntotal:\t\t\t\t\t\t\t\t\t\t"
                    "internal fragments:%10luK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                    "memory blowup:\t\t\t\t\t\t%10ldK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                    "external fragment:\t\t\t\t%10ldK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                    "real using memory:\t\t\t\t%10ldK(%3d%%)\n"
                    "\ntotal:\t\t\t\t\t\t\t\t\t\t"
                    "active alloc: %10lu\t\t\tfreelist objects: %10lu\t\t\t"
                    "new allocated: %10lu\t\t\treused allocated: %10lu\t\t\tfreed: %10lu\n",
            mem_alloc_wasted_record_total/1024, mem_alloc_wasted_record_total*100/totalMem,
            mem_blowup_total/1024, mem_blowup_total*100/totalMem,
            exfrag/1024, exfrag*100/totalMem,
            realMem/1024,
            100 - mem_alloc_wasted_record_total*100/totalMem - mem_blowup_total*100/totalMem - exfrag*100/totalMem,
            num_alloc_active_total, num_freelist_total,
            num_alloc_total, num_allocFFL_total, num_free_total);

}

