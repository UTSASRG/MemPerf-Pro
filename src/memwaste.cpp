//
// Created by 86152 on 2020/2/22.
//
//#include <atomic>
#include <stdio.h>
#include "memwaste.h"

#define MAX_THREAD_NUMBER 1024

HashMap <void*, objStatus, spinlock, PrivateHeap> MemoryWaste::objStatusMap;
uint64_t* MemoryWaste::mem_alloc_wasted;
uint64_t* MemoryWaste::mem_alloc_wasted_record;
uint64_t* MemoryWaste::mem_alloc_wasted_record_global;

int64_t * MemoryWaste::num_alloc_active;
int64_t * MemoryWaste::num_alloc_active_record;
int64_t * MemoryWaste::num_alloc_active_record_global;

int64_t * MemoryWaste::num_freelist;
int64_t * MemoryWaste::num_freelist_record;
int64_t * MemoryWaste::num_freelist_record_global;

uint64_t * MemoryWaste::num_alloc;
uint64_t * MemoryWaste::num_allocFFL;
uint64_t * MemoryWaste::num_free;

uint64_t * MemoryWaste::num_alloc_record;
uint64_t * MemoryWaste::num_allocFFL_record;
uint64_t * MemoryWaste::num_free_record;

uint64_t * MemoryWaste::num_alloc_record_global;
uint64_t * MemoryWaste::num_allocFFL_record_global;
uint64_t * MemoryWaste::num_free_record_global;

int64_t * MemoryWaste::blowupflag_record;
int64_t * MemoryWaste::blowupflag;

uint64_t MemoryWaste::mem_alloc_wasted_record_total = 0;
uint64_t MemoryWaste::mem_blowup_total = 0;

uint64_t MemoryWaste::num_alloc_active_total = 0;
uint64_t MemoryWaste::num_freelist_total = 0;

uint64_t MemoryWaste::num_alloc_total = 0;
uint64_t MemoryWaste::num_allocFFL_total = 0;
uint64_t MemoryWaste::num_free_total = 0;

spinlock MemoryWaste::record_lock;

long MemoryWaste::realMem = 0;
long MemoryWaste::totalMem = 0;

extern int threadcontention_index;

void MemoryWaste::initialize() {
    objStatusMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);

    mem_alloc_wasted = (uint64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(uint64_t));
    mem_alloc_wasted_record = (uint64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(uint64_t));
    mem_alloc_wasted_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

    num_alloc_active = (int64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(int64_t));
    num_alloc_active_record = (int64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(int64_t));
    num_alloc_active_record_global = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));

    num_freelist = (int64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(int64_t));
    num_freelist_record = (int64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(int64_t));
    num_freelist_record_global = (int64_t*) myMalloc(num_class_sizes * sizeof(int64_t));

    num_alloc = (uint64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(uint64_t));
    num_allocFFL = (uint64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(uint64_t));
    num_free = (uint64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(uint64_t));

    num_alloc_record = (uint64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(uint64_t));
    num_allocFFL_record = (uint64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(uint64_t));
    num_free_record = (uint64_t*) myMalloc(MAX_THREAD_NUMBER * num_class_sizes * sizeof(uint64_t));

    num_alloc_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_allocFFL_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    num_free_record_global = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));

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
///Here

        num_alloc[(allocData->tid*num_class_sizes)+classSizeIndex]++;

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
///Here
        num_allocFFL[(allocData->tid*num_class_sizes)+classSizeIndex]++;
        num_freelist[(allocData->tid*num_class_sizes)+classSizeIndex]--;


        status->size_using = allocData->size;
        status->classSize = classSize;
        status->classSizeIndex = classSizeIndex;

    }

    num_alloc_active[(allocData->tid*num_class_sizes)+classSizeIndex]++;

    if(status->max_touched_bytes < allocData->size) {
        status->max_touched_bytes = allocData->size;
    }
    mem_alloc_wasted[(allocData->tid*num_class_sizes)+classSizeIndex] += classSize-allocData->size;
    if(classSize-status->max_touched_bytes >= PAGESIZE && classSize >= status->max_touched_bytes) {
        mem_alloc_wasted[(allocData->tid*num_class_sizes)+classSizeIndex] -= (classSize-status->max_touched_bytes)/PAGESIZE*PAGESIZE;
    } else if(classSize-allocData->size >= PAGESIZE) {
        mem_alloc_wasted[(allocData->tid*num_class_sizes)+classSizeIndex] -= (classSize-allocData->size)/PAGESIZE*PAGESIZE;
    }


        if( blowupflag[classSizeIndex] > 0) {
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
    ///Here
    num_free[(allocData->tid*num_class_sizes)+classSizeIndex]++;
    num_freelist[(allocData->tid*num_class_sizes)+classSizeIndex]++;
    num_alloc_active[(allocData->tid*num_class_sizes)+classSizeIndex]--;

    allocData->size = size;
    allocData->classSize = classSize;

    if(classSize-status->max_touched_bytes >= PAGESIZE && classSize >= status->max_touched_bytes) {
        mem_alloc_wasted[(allocData->tid*num_class_sizes)+classSizeIndex] += (classSize-status->max_touched_bytes)/PAGESIZE*PAGESIZE;
    } else if(classSize-allocData->size >= PAGESIZE) {
        mem_alloc_wasted[(allocData->tid*num_class_sizes)+classSizeIndex] += (classSize-allocData->size)/PAGESIZE*PAGESIZE;
    }
    mem_alloc_wasted[(allocData->tid*num_class_sizes)+classSizeIndex] -= classSize-size;


//    status->size_using = 0;
//    status->classSize = classSize;

        blowupflag[classSizeIndex]++;

}


bool MemoryWaste::recordMemory(long realMemory, long totalMemory) {

    record_lock.lock();

realMem = realMemory;
totalMem = totalMemory;


    memcpy(mem_alloc_wasted_record, mem_alloc_wasted, (threadcontention_index+1) * num_class_sizes * sizeof(uint64_t));

    memcpy(num_alloc_record, num_alloc, (threadcontention_index+1) * num_class_sizes * sizeof(uint64_t));
    memcpy(num_allocFFL_record, num_allocFFL, (threadcontention_index+1) * num_class_sizes * sizeof(uint64_t));
    memcpy(num_free_record, num_free, (threadcontention_index+1) * num_class_sizes * sizeof(uint64_t));

    memcpy(num_alloc_active_record, num_alloc_active, (threadcontention_index+1) * num_class_sizes * sizeof(int64_t));
    memcpy(num_freelist_record, num_freelist, (threadcontention_index+1) * num_class_sizes * sizeof(int64_t));

    memcpy(blowupflag_record, blowupflag, num_class_sizes * sizeof(int64_t));

    record_lock.unlock();


    return true;
}

extern size_t * class_sizes;

uint64_t MemoryWaste::recordSumup() {
    for (int t = 0; t <= threadcontention_index; t++) {
    for (int i = 0; i < num_class_sizes; ++i) {
            mem_alloc_wasted_record_global[i] += mem_alloc_wasted_record[t * num_class_sizes + i];
            num_alloc_active_record_global[i] += num_alloc_active_record[t * num_class_sizes + i];
            num_freelist_record_global[i] += num_freelist_record[t * num_class_sizes + i];
            num_alloc_record_global[i] += num_alloc_record[t * num_class_sizes + i];
            num_allocFFL_record_global[i] += num_allocFFL_record[t * num_class_sizes + i];
            num_free_record_global[i] += num_free_record[t * num_class_sizes + i];
        }
    }
    for (int i = 0; i < num_class_sizes; ++i) {
        if(mem_alloc_wasted_record_global[i] < 0) {
            mem_alloc_wasted_record_global[i] = 0;
        }
        if(num_alloc_active_record_global[i] < 0) {
            num_alloc_active_record_global[i] = 0;
        }
        if(num_freelist_record_global[i] < 0) {
            num_freelist_record_global[i] = 0;
        }
        if(blowupflag_record[i] < 0) {
            blowupflag_record[i] = 0;
        }

        mem_alloc_wasted_record_total += mem_alloc_wasted_record_global[i];
        num_alloc_active_total += num_alloc_active_record_global[i];
        num_freelist_total += num_freelist_record_global[i];
        num_alloc_total += num_alloc_record_global[i];
        num_allocFFL_total += num_allocFFL_record_global[i];
        num_free_total += num_free_record_global[i];

    }

    return mem_alloc_wasted_record_total;
}


void MemoryWaste::reportMaxMemory(FILE * output) {

    if(output == nullptr) {
        output = stderr;
    }

    fprintf (output, "\n>>>>>>>>>>>>>>>    MEMORY DISTRIBUTION    <<<<<<<<<<<<<<<\n");
    //fprintf(output, record_time);
    for (int i = 0; i < num_class_sizes; ++i) {
        int64_t blowup = class_sizes[i] * (num_freelist_record_global[i] - blowupflag_record[i]);
            if(blowup < 0 || blowup > totalMem - realMem - mem_alloc_wasted_record_total) {
                blowup = 0;
            }

        if(mem_alloc_wasted_record_global[i]/1024 == 0 && blowup/1024 == 0 &&
                num_alloc_active_record_global[i] == 0 && num_freelist_record_global[i] == 0 ) {
            continue;
        }

            fprintf(output, "size: %10lu\t\t\t", class_sizes[i]);

        fprintf(output, "internal fragmentation: %10luK\t\t\tmemory blowup: %10luK\t\t\t"
                        "active alloc: %10ld\t\t\tfreelist objects: %10ld\t\t\t"
                        "new allocated: %10lu\t\t\treused allocated: %10lu\t\t\tfreed: %10lu\n",
                mem_alloc_wasted_record_global[i]/1024, blowup/1024,
                num_alloc_active_record_global[i], num_freelist_record_global[i],
                num_alloc_record_global[i], num_allocFFL_record_global[i], num_free_record_global[i]);

        mem_blowup_total += blowup;
    }

    int64_t exfrag = totalMem - realMem - mem_alloc_wasted_record_total - mem_blowup_total;
    if(exfrag < 0) {
        exfrag = 0;
    }
    if(totalMem == 0) {
        totalMem = 1;
    }
    fprintf(output, "\ntotal:\t\t\t\t\t\t\t\t\t\t"
                    "total internal fragmentation:%10luK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t",
            mem_alloc_wasted_record_total/1024, mem_alloc_wasted_record_total*100/totalMem);
        fprintf(output, "total memory blowup:\t\t\t\t\t\t%10ldK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                        "total external fragmentation:\t\t\t\t%10ldK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t",
                mem_blowup_total/1024, mem_blowup_total*100/totalMem,
                exfrag/1024, exfrag*100/totalMem);
    fprintf(output,
            "total real using memory:\t\t\t\t%10ldK(%3d%%)\n\t\t\t\t\t\t\t\t\t\t\t\t\t"
                    "total using memory:\t\t\t\t%10ldK\n"
                    "\ncurrent status:\t\t\t\t\t\t\t\t\t\t"
                    "active objects: %10lu\t\t\tfreelist objects: %10lu"
                    "\naccumulative results:\t\t\t\t\t\t\t\t\t\t"
                    "new allocated: %10lu\t\t\treused allocated: %10lu\t\t\tfreed: %10lu\n",
            realMem/1024,
            100 - mem_alloc_wasted_record_total*100/totalMem - mem_blowup_total*100/totalMem - exfrag*100/totalMem,
            totalMem/1024,
            num_alloc_active_total, num_freelist_total,
            num_alloc_total, num_allocFFL_total, num_free_total);

}

