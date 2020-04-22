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
    bool inBlowup;
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
    const static uint64_t stride = ONE_MEGABYTE;

    static thread_local uint64_t * mem_alloc_wasted_record;
    static thread_local uint64_t * mem_alloc_wasted_record_minus;
    static thread_local uint64_t * mem_freelist_wasted_record;
    static thread_local uint64_t * mem_freelist_wasted_record_minus;

    static uint64_t * mem_alloc_wasted_record_global;
    static uint64_t * mem_freelist_wasted_record_global;
    static uint64_t * mem_alloc_wasted_record_global_minus;
    static uint64_t * mem_freelist_wasted_record_global_minus;

    static thread_local uint64_t * mem_blowup;
    static thread_local uint64_t * mem_blowup_minus;
    static thread_local uint64_t * mem_blowup_record;
    static thread_local uint64_t * mem_blowup_minus_record;
    static uint64_t * mem_blowup_global;
    static uint64_t * mem_blowup_global_minus;
    static int64_t * free_nums;

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
    static void reportMaxMemory(FILE * output);
};

#endif //MMPROF_MEMWASTE_H
