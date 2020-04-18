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
    static thread_local bool thread_init;
    static bool global_init;
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

    static uint64_t * mem_blowup;
    static uint64_t * mem_blowup_global;
    static int64_t * free_nums;

public:

    static void initialize();
///Here
    static void initForNewTid();
    static bool allocUpdate(allocation_metadata * allocData, void * address);
    static void freeUpdate(allocation_metadata * allocData, void* address);
    ///Here
    static bool recordMemory(uint64_t now_usage);
    static void globalizeMemory();
    static void reportMaxMemory(FILE * output);
    static void checkGlobalInit();
    static void checkThreadInit();
};

#endif //MMPROF_MEMWASTE_H
