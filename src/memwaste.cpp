//
// Created by 86152 on 2020/2/22.
//
#include <atomic>
#include <stdio.h>
#include "memwaste.h"

HashMap <void*, obj_status*, spinlock> MemoryWaste::addr_obj_status;
uint64_t* MemoryWaste::mem_alloc_wasted;
uint64_t* MemoryWaste::mem_freelist_wasted;
///Here
spinlock MemoryWaste::record_lock;
uint64_t MemoryWaste::now_max_usage;
const uint64_t MemoryWaste::stride;

uint64_t* MemoryWaste::mem_alloc_wasted_record;
uint64_t* MemoryWaste::mem_freelist_wasted_record;

obj_status * MemoryWaste::newObjStatus(size_t size_using, size_t classSize, short classSizeIndex) {
    obj_status * ptr = (obj_status*) malloc(sizeof(obj_status));
//    ptr->tid = tid;
    ptr->size_using = size_using;
    ptr->classSize = classSize;
    ptr->classSizeIndex = classSizeIndex;
    return ptr;
}

void MemoryWaste::initialize() {

    addr_obj_status.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);

    if(bibop) {
        mem_alloc_wasted = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    } else {
        mem_alloc_wasted = (uint64_t*) myMalloc(2 * sizeof(uint64_t));
    }
    if(bibop) {
        mem_freelist_wasted = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    } else {
        mem_freelist_wasted = (uint64_t*) myMalloc(2 * sizeof(uint64_t));
    }

    if(bibop) {
        mem_alloc_wasted_record = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    } else {
        mem_alloc_wasted_record = (uint64_t*) myMalloc(2 * sizeof(uint64_t));
    }
    if(bibop) {
        mem_freelist_wasted_record = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    } else {
        mem_freelist_wasted_record = (uint64_t*) myMalloc(2 * sizeof(uint64_t));
    }
    record_lock.init();
    now_max_usage = 0;
}

void getClassSizeForStyles(size_t size, void* uintaddr, size_t * classSize, short * classSizeIndex);

bool MemoryWaste::allocUpdate(size_t size, size_t* out_classSize, short* out_classSizeIndex, void * address) {
    bool reused;
    size_t classSize;
    short classSizeIndex;
    /* New or Reused? Get old status */
    obj_status * old_status;
    if(! addr_obj_status.find(address, &old_status)) {
        reused = false;
        /* new status */
        getClassSizeForStyles(size, address, out_classSize, out_classSizeIndex);
        classSize = *out_classSize;
        classSizeIndex = *out_classSizeIndex;
        addr_obj_status.insert(address, newObjStatus(size, classSize, classSizeIndex));
    } else {
        reused = true;
        ///Here
        if(old_status->size_using == size) {
            *out_classSize = old_status->classSize;
            *out_classSizeIndex = old_status->classSizeIndex;
        } else {
            getClassSizeForStyles(size, address, out_classSize, out_classSizeIndex);
        }

        classSize = *out_classSize;
        classSizeIndex = *out_classSizeIndex;

        if(mem_freelist_wasted[classSizeIndex] <= classSize) {
            //the_mem_freelist_wasted_new[classSizeIndex] = 0;
            mem_freelist_wasted[classSizeIndex] = 0;
        } else {
            //the_mem_freelist_wasted_new[classSizeIndex] -= classSize;
            mem_freelist_wasted[classSizeIndex] -= classSize;
        }

        old_status->size_using = size;
//        new_status->classSize = classSize;
        old_status->classSize = classSize;
        old_status->classSizeIndex = classSizeIndex;
//        addr_obj_status.erase(address);
//        addr_obj_status.insert(address, new_status);
    }

    if(classSize-size >= PAGESIZE) {
        //the_mem_alloc_wasted[classSizeIndex] += (classSize/PAGESIZE)*PAGESIZE-size;
        mem_alloc_wasted[classSizeIndex] += (classSize/PAGESIZE)*PAGESIZE-size;
    } else {
        //the_mem_alloc_wasted[classSizeIndex] += classSize-size;
        mem_alloc_wasted[classSizeIndex] += classSize-size;
    }

    return reused;
}

void MemoryWaste::freeUpdate(void* address) {

    /* Get old status */
    obj_status* old_status;
    if (! addr_obj_status.find(address, &old_status)) {
        fprintf(stderr, "addr_obj_status key error: %p\n", address);
        abort();
    }
    size_t size = old_status->size_using;
    size_t classSize = old_status->classSize;
    size_t classSizeIndex = old_status->classSizeIndex;

    if(classSize-size >= PAGESIZE) {
        //the_mem_alloc_wasted[classSizeIndex] -= (classSize/PAGESIZE)*PAGESIZE-size;
        mem_alloc_wasted[classSizeIndex] -= (classSize/PAGESIZE)*PAGESIZE-size;
    } else {
        //the_mem_alloc_wasted[classSizeIndex] -= classSize - size;
        mem_alloc_wasted[classSizeIndex] -= classSize - size;
    }

    mem_freelist_wasted[classSizeIndex] += classSize;

    old_status->size_using = 0;
//    new_status->classSize = classSize;
    old_status->classSize = classSize;
}

///Here
bool MemoryWaste::recordMemory(uint64_t now_usage) {
    if(now_usage <= now_max_usage + stride) {
        return false;
    }

    record_lock.lock();

    now_max_usage = now_usage;

    if(bibop) {
        memcpy(mem_alloc_wasted_record, mem_alloc_wasted, num_class_sizes * sizeof(uint64_t));
        memcpy(mem_freelist_wasted_record, mem_freelist_wasted, num_class_sizes * sizeof(uint64_t));
    } else {
        memcpy(mem_alloc_wasted_record, mem_alloc_wasted, 2 * sizeof(uint64_t));
        memcpy(mem_freelist_wasted_record, mem_freelist_wasted, 2 * sizeof(uint64_t));
    }

    record_lock.unlock();
    return true;
}

void MemoryWaste::reportMaxMemory(FILE * output) {

    if(output == nullptr) {
        output = stderr;
    }

    fprintf (output, "\n>>>>>>>>>>>>>>>    Memory Distribution    <<<<<<<<<<<<<<<\n");
    if(bibop) {
        for (int i = 0; i < num_class_sizes; ++i)
            fprintf(output, "idx: %10d, alloc wasted: %10u, freelist wasted: %10u\n",
                    i, mem_alloc_wasted_record[i], mem_freelist_wasted_record[i]);
    } else {
        for (int i = 0; i < 2; ++i)
            fprintf(output, "idx: %10d, alloc wasted: %10u, freelist wasted: %10u\n",
                    i, mem_alloc_wasted_record[i], mem_freelist_wasted_record[i]);
    }

}

size_t MemoryWaste::getSize(void * address) {
    obj_status * status;
    if(addr_obj_status.find(address, &status)) {
        return status->size_using;
    } else {
        return 0;
    }
}

size_t MemoryWaste::getClassSize(void * address) {
    obj_status * status;
    if(addr_obj_status.find(address, &status)) {
        return status->classSize;
    } else {
        return 0;
    }
}