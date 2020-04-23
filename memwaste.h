//
// Created by 86152 on 2020/2/22.
//

#ifndef MMPROF_MEMWASTE_H
#define MMPROF_MEMWASTE_H


#include <atomic>
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "libmallocprof.h"
#include "memsample.h"

#define MAX_OBJ_NUM 4096*4*64
//#define MAX_PAGE_NUM 4096
//#define MAX_THREAD_NUMBER 2048


extern int num_class_sizes;

typedef struct {
    //pid_t tid;
    size_t size_using;
    size_t classSize;
    short classSizeIndex;
} objStatus;

class MemoryWaste{
private:
    static HashMap <void*, objStatus, spinlock, PrivateHeap> objStatusMap;
    static thread_local uint64_t* mem_alloc_wasted;
    static thread_local uint64_t* mem_alloc_wasted_minus;
    static thread_local uint64_t* mem_freelist_wasted;
    static thread_local uint64_t* mem_freelist_wasted_minus;
///Here
    static spinlock record_lock;
    static thread_local uint64_t now_max_usage;
    //const static uint64_t stride = ONE_MEGABYTE;
    const static uint64_t stride = 0;

    static thread_local uint64_t * mem_alloc_wasted_record;
    static thread_local uint64_t * mem_alloc_wasted_record_minus;
    static thread_local uint64_t * mem_freelist_wasted_record;
    static thread_local uint64_t * mem_freelist_wasted_record_minus;

    static uint64_t * mem_alloc_wasted_record_global;
    static uint64_t * mem_freelist_wasted_record_global;
    static uint64_t * mem_alloc_wasted_record_global_minus;
    static uint64_t * mem_freelist_wasted_record_global_minus;

    static thread_local int64_t * num_alloc_active;
    static thread_local int64_t * num_alloc_active_record;
    static int64_t * num_alloc_active_global;
    static thread_local int64_t * num_freelist;
    static thread_local int64_t * num_freelist_record;
    static int64_t * num_freelist_global;

    static thread_local uint64_t * num_alloc;
    static thread_local uint64_t * num_allocFFL;
    static thread_local uint64_t * num_free;
    static uint64_t * num_alloc_global;
    static uint64_t * num_allocFFL_global;
    static uint64_t * num_free_global;

    static thread_local uint64_t * num_alloc_record;
    static thread_local uint64_t * num_allocFFL_record;
    static thread_local uint64_t * num_free_record;

    static uint64_t * num_alloc_record_global;
    static uint64_t * num_allocFFL_record_global;
    static uint64_t * num_free_record_global;

    static int64_t * blowupflag_record;
    static int64_t * blowupflag;

public:

    static void initialize();
///Here
    static void initForNewTid();
    static bool allocUpdate(allocation_metadata * allocData, void * address);
    static void freeUpdate(allocation_metadata * allocData, void* address);
    ///Here
    static bool recordMemory(uint64_t now_usage);
    static void globalizeMemory();
    static void reportAllocDistribution(FILE * output);
    static void reportMaxMemory(FILE * output, long realMem, long totalMem);
};

#endif //MMPROF_MEMWASTE_H
