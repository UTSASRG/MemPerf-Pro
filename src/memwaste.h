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

#define MAX_OBJ_NUM 4096*4
#define MAX_PAGE_NUM 4096
#define MAX_THREAD_NUMBER 2048


extern int num_class_sizes;

typedef struct {
    size_t remain_size;
    pid_t tid;
    size_t size_using;
    size_t classSize;
} obj_status;

class MemoryWaste{
private:
    static void initForNewTid(pid_t tid);
    static void initForNewPage(void* pageidx);
    static obj_status * newObjStatus(size_t remain_size, pid_t tid, size_t size_using, size_t classSize);

public:
    static HashMap <void*, HashMap<void*, bool*, spinlock>*, spinlock> objects_each_page;
    static HashMap <void*, obj_status*, spinlock> addr_obj_status;
    static HashMap <pid_t, std::atomic<uint64_t>*, spinlock> mem_alloc_real_using;
    static HashMap <pid_t, std::atomic<uint64_t>*, spinlock> mem_alloc_wasted;
    static HashMap <pid_t, std::atomic<uint64_t>*, spinlock> mem_freelist_wasted;
    static std::atomic<uint64_t> * mem_never_used;
    static void initialize();
    static bool allocUpdate(pid_t tid, size_t size, void * address);
    static void freeUpdate(pid_t tid, void* address);
    static void reportMemory();
};

#endif //MMPROF_MEMWASTE_H
