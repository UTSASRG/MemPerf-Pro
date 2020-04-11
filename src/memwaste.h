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
} obj_status;

class MemoryWaste{
private:
    static HashMap <void*, obj_status*, spinlock> addr_obj_status;
    //static HashMap <pid_t, std::atomic<uint64_t>*, spinlock> mem_alloc_real_using;
//    static HashMap <pid_t, std::atomic<uint64_t>*, spinlock> mem_alloc_wasted;
//    static HashMap <pid_t, std::atomic<uint64_t>*, spinlock> mem_freelist_wasted;
///Here
//    static HashMap <pid_t, std::atomic<uint64_t>*, nolock> mem_alloc_wasted;
//    static HashMap <pid_t, std::atomic<uint64_t>*, nolock> mem_freelist_wasted;
    static uint64_t* mem_alloc_wasted;
    static uint64_t* mem_freelist_wasted;
///Here
    static spinlock record_lock;
    static uint64_t now_max_usage;
    const static uint64_t stride = ONE_MEGABYTE;

//    static HashMap <pid_t, uint64_t*, spinlock> mem_alloc_real_using_record;
//    static HashMap <pid_t, uint64_t*, spinlock> mem_alloc_wasted_record;
//    static HashMap <pid_t, uint64_t*, spinlock> mem_freelist_wasted_record;
///Here
//    static HashMap <pid_t, uint64_t*, nolock> mem_alloc_wasted_record;
//    static HashMap <pid_t, uint64_t*, nolock> mem_freelist_wasted_record;
    static uint64_t * mem_alloc_wasted_record;
    static uint64_t * mem_freelist_wasted_record;
///Here
//    static void initForNewTid(pid_t tid);
    static obj_status * newObjStatus(size_t size_using, size_t classSize, short classSizeIndex);

public:
    static void initialize();
    static bool allocUpdate(size_t size, size_t* out_classSize, short* out_classSizeIndex, void * address);
    static void freeUpdate(void* address);
    ///Here
    static bool recordMemory(uint64_t now_usage);
    //static void reportMemory(FILE * output);
    static void reportMaxMemory(FILE * output);
    static size_t getSize(void * address);
    static size_t getClassSize(void * address);
};

#endif //MMPROF_MEMWASTE_H
