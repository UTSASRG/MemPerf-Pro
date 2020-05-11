//
// Created by 86152 on 2020/2/22.
//

#ifndef MMPROF_MEMWASTE_H
#define MMPROF_MEMWASTE_H


//#include <atomic>
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "libmallocprof.h"
#include "memsample.h"

#define MAX_OBJ_NUM 4096*4*64
//#define MAX_PAGE_NUM 4096


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
    static spinlock record_lock;
    static char record_time[1024];

    static uint64_t* mem_alloc_wasted;
    static uint64_t * mem_alloc_wasted_record;
    static uint64_t * mem_alloc_wasted_record_global;

    static int64_t * num_alloc_active;
    static int64_t * num_alloc_active_record;
    static int64_t * num_alloc_active_record_global;

    static int64_t * num_freelist;
    static int64_t * num_freelist_record;
    static int64_t * num_freelist_record_global;

    static uint64_t * num_alloc;
    static uint64_t * num_allocFFL;
    static uint64_t * num_free;

    static uint64_t * num_alloc_record;
    static uint64_t * num_allocFFL_record;
    static uint64_t * num_free_record;

    static uint64_t * num_alloc_record_global;
    static uint64_t * num_allocFFL_record_global;
    static uint64_t * num_free_record_global;

    static int64_t * blowupflag_record;
    static int64_t * blowupflag;

    static uint64_t mem_alloc_wasted_record_total;
    static uint64_t mem_blowup_total;

    static uint64_t num_alloc_active_total;
    static uint64_t num_freelist_total;

    static uint64_t num_alloc_total;
    static uint64_t num_allocFFL_total;
    static uint64_t num_free_total;

    static long realMem;

public:

    static void initialize();
///Here
    static bool allocUpdate(allocation_metadata * allocData, void * address);
    static void freeUpdate(allocation_metadata * allocData, void* address);
    ///Here
    static bool recordMemory(long totalMemory);
    static uint64_t recordSumup();
    static void reportMaxMemory(FILE * output, long realMem);
};

#endif //MMPROF_MEMWASTE_H
