//
// Created by 86152 on 2020/2/22.
//
#include <atomic>
#include <stdio.h>
#include "memwaste.h"

HashMap <void*, obj_status*, spinlock> MemoryWaste::addr_obj_status;
HashMap <pid_t, std::atomic<uint64_t>*, spinlock> MemoryWaste::mem_alloc_real_using;
HashMap <pid_t, std::atomic<uint64_t>*, spinlock> MemoryWaste::mem_alloc_wasted;
HashMap <pid_t, std::atomic<uint64_t>*, spinlock> MemoryWaste::mem_freelist_wasted;

spinlock MemoryWaste::record_lock;
std::atomic<uint64_t> MemoryWaste::now_max_usage;
const uint64_t MemoryWaste::stride;
HashMap <pid_t, uint64_t*, spinlock> MemoryWaste::mem_alloc_real_using_record;
HashMap <pid_t, uint64_t*, spinlock> MemoryWaste::mem_alloc_wasted_record;
HashMap <pid_t, uint64_t*, spinlock> MemoryWaste::mem_freelist_wasted_record;

obj_status * MemoryWaste::newObjStatus(pid_t tid, size_t size_using, size_t classSize) {
    obj_status * ptr = (obj_status*) malloc(sizeof(obj_status));
    ptr->tid = tid;
    ptr->size_using = size_using;
    ptr->classSize = classSize;
    return ptr;
}

void MemoryWaste::initialize() {

    addr_obj_status.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);

    mem_alloc_real_using.initialize(HashFuncs::hashInt, HashFuncs::compareInt, MAX_THREAD_NUMBER);
    mem_alloc_wasted.initialize(HashFuncs::hashInt, HashFuncs::compareInt, MAX_THREAD_NUMBER);
    mem_freelist_wasted.initialize(HashFuncs::hashInt, HashFuncs::compareInt, MAX_THREAD_NUMBER);

    mem_alloc_real_using_record.initialize(HashFuncs::hashInt, HashFuncs::compareInt, MAX_THREAD_NUMBER);
    mem_alloc_wasted_record.initialize(HashFuncs::hashInt, HashFuncs::compareInt, MAX_THREAD_NUMBER);
    mem_freelist_wasted_record.initialize(HashFuncs::hashInt, HashFuncs::compareInt, MAX_THREAD_NUMBER);

    now_max_usage = 0;
}

void MemoryWaste::initForNewTid(pid_t tid) {
    std::atomic<uint64_t>* tmp_addr;

    if(bibop) {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(num_class_sizes * sizeof(std::atomic<uint64_t>));
    } else {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(2 * sizeof(std::atomic<uint64_t>));
    }
    mem_alloc_real_using.insert(tid, tmp_addr);
    if(bibop) {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(num_class_sizes * sizeof(std::atomic<uint64_t>));
    } else {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(2 * sizeof(std::atomic<uint64_t>));
    }
    mem_alloc_wasted.insert(tid, tmp_addr);
    if(bibop) {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(num_class_sizes * sizeof(std::atomic<uint64_t>));
    } else {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(2 * sizeof(std::atomic<uint64_t>));
    }
    mem_freelist_wasted.insert(tid, tmp_addr);

    uint64_t * tmp_addr2;
    if(bibop) {
        tmp_addr2 = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    } else {
        tmp_addr2 = (uint64_t*) myMalloc(2 * sizeof(uint64_t));
    }
    mem_alloc_real_using_record.insert(tid, tmp_addr2);
    if(bibop) {
        tmp_addr2 = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    } else {
        tmp_addr2 = (uint64_t*) myMalloc(2 * sizeof(uint64_t));
    }
    mem_alloc_wasted_record.insert(tid, tmp_addr2);
    if(bibop) {
        tmp_addr2 = (uint64_t*) myMalloc(num_class_sizes * sizeof(uint64_t));
    } else {
        tmp_addr2 = (uint64_t*) myMalloc(2 * sizeof(uint64_t));
    }
    mem_freelist_wasted_record.insert(tid, tmp_addr2);
}

bool MemoryWaste::allocUpdate(pid_t tid, size_t size, void * address) {
    bool reused;
    size_t classSize;
    if(bibop) {
        classSize = getClassSizeFor(size);
    } else {
        classSize = malloc_usable_size(address);
    }
    short classSizeIndex = getClassSizeIndex(classSize);

    /* mem_alloc_real_using */
    std::atomic<uint64_t>* the_mem_alloc_real_using;
    if(! mem_alloc_real_using.find(tid, &the_mem_alloc_real_using)) {
        initForNewTid(tid);
    }
    if (! mem_alloc_real_using.find(tid, &the_mem_alloc_real_using)) {
        fprintf(stderr, "the_mem_alloc_real_using key error: %d\n", tid);
        abort();
    }
    the_mem_alloc_real_using[classSizeIndex] += size;

    /* mem_alloc_wasted */
    std::atomic<uint64_t>* the_mem_alloc_wasted;
    if (! mem_alloc_wasted.find(tid, &the_mem_alloc_wasted)) {
        fprintf(stderr, "mem_alloc_wasted key error: %d\n", tid);
        abort();
    }
    if(classSize-size >= PAGESIZE) {
        the_mem_alloc_wasted[classSizeIndex] += (classSize/PAGESIZE)*PAGESIZE-size;
    } else {
        the_mem_alloc_wasted[classSizeIndex] += classSize-size;
    }

    /* New or Reused? Get old status */
    obj_status * old_status;
    if(! addr_obj_status.find(address, &old_status)) {
        reused = false;
        /* new status */
        addr_obj_status.insert(address, newObjStatus(tid, size, classSize));
    } else {
        reused = true;
        std::atomic<uint64_t>* the_mem_freelist_wasted_new;
        if(! mem_freelist_wasted.find(tid, &the_mem_freelist_wasted_new)) {
            fprintf(stderr, "mem_freelist_wasted key error: %d\n", tid);
            abort();
        }
        /* freelist_wasted[newthread] */
        if(the_mem_freelist_wasted_new[classSizeIndex] <= classSize) {
            the_mem_freelist_wasted_new[classSizeIndex] = 0;
        } else {
            the_mem_freelist_wasted_new[classSizeIndex] -= classSize;
        }
        /* new status */
        obj_status * new_status = (obj_status*)malloc(sizeof(obj_status));
        new_status->tid = tid;
        new_status->size_using = size;
        new_status->classSize = classSize;
        addr_obj_status.erase(address);
        addr_obj_status.insert(address, new_status);
    }
    return reused;
}

void MemoryWaste::freeUpdate(pid_t tid, void* address) {

    /* Get old status */
    obj_status* old_status;
    if (! addr_obj_status.find(address, &old_status)) {
        fprintf(stderr, "addr_obj_status key error: %p\n", address);
        abort();
    }
    size_t size = old_status->size_using;
    size_t classSize = old_status->classSize;
    short classSizeIndex = getClassSizeIndex(classSize);


    /* mem_alloc_real_using[oldthread] */
    std::atomic<uint64_t>* the_mem_alloc_real_using;
    if( ! mem_alloc_real_using.find(old_status->tid, &the_mem_alloc_real_using)) {
        fprintf(stderr, "mem_alloc_real_using key error: %d\n", old_status->tid);
        abort();
    }
    the_mem_alloc_real_using[classSizeIndex] -= size;

    /* mem_alloc_wasted */
    std::atomic<uint64_t>* the_mem_alloc_wasted;
    if(! mem_alloc_wasted.find(old_status->tid, &the_mem_alloc_wasted)) {
        fprintf(stderr, "mem_alloc_wasted key error: %d\n", old_status->tid);
        abort();
    }
    if(classSize-size >= PAGESIZE) {
        the_mem_alloc_wasted[classSizeIndex] -= (classSize/PAGESIZE)*PAGESIZE-size;
    } else {
        the_mem_alloc_wasted[classSizeIndex] -= classSize - size;
    }

    std::atomic<uint64_t>* the_mem_freelist_wasted_new;
    if(! mem_freelist_wasted.find(tid, &the_mem_freelist_wasted_new)) {
        fprintf(stderr, "mem_freelist_wasted key error: %d\n", tid);
        abort();
    }

    /* freelist_wasted[newthread] */
    the_mem_freelist_wasted_new[classSizeIndex] += classSize;

    /* new status */
    obj_status * new_status = (obj_status*)malloc(sizeof(obj_status));
    new_status->tid = tid;
    new_status->size_using = 0;
    new_status->classSize = classSize;
    addr_obj_status.erase(address);
    addr_obj_status.insert(address, new_status);
}

bool MemoryWaste::recordMemory(uint64_t now_usage) {
    if(now_usage <= now_max_usage + stride) {
        return false;
    }
    now_max_usage = now_usage;

    record_lock.lock();
    for(auto tid_and_values : mem_alloc_real_using) {

        pid_t tid = tid_and_values.getKey();
        std::atomic<uint64_t>* the_mem_alloc_real_using = tid_and_values.getData();
        std::atomic<uint64_t>* the_mem_alloc_wasted;
        std::atomic<uint64_t>* the_mem_freelist_wasted;
        mem_alloc_wasted.find(tid, &the_mem_alloc_wasted);
        mem_freelist_wasted.find(tid, &the_mem_freelist_wasted);

        uint64_t * the_mem_alloc_real_using_record;
        uint64_t * the_mem_alloc_wasted_record;
        uint64_t * the_mem_freelist_wasted_record;
        mem_alloc_real_using_record.find(tid, &the_mem_alloc_real_using_record);
        mem_alloc_wasted_record.find(tid, &the_mem_alloc_wasted_record);
        mem_freelist_wasted_record.find(tid, &the_mem_freelist_wasted_record);

        if(bibop) {
            for (int i = 0; i < num_class_sizes; ++i) {
                the_mem_alloc_real_using_record[i] = (uint64_t)the_mem_alloc_real_using[i];
                the_mem_alloc_wasted_record[i] = (uint64_t)the_mem_alloc_wasted[i];
                the_mem_freelist_wasted_record[i] = (uint64_t)the_mem_freelist_wasted[i];
            }
        } else {
            for(int i = 0; i < 2; ++i) {
                the_mem_alloc_real_using_record[i] = (uint64_t)the_mem_alloc_real_using[i];
                the_mem_alloc_wasted_record[i] = (uint64_t)the_mem_alloc_wasted[i];
                the_mem_freelist_wasted_record[i] = (uint64_t)the_mem_freelist_wasted[i];}
        }
    }
    record_lock.unlock();
    return true;
}

void MemoryWaste::reportMaxMemory(FILE * output) {

    if(output == nullptr) {
        output = stderr;
    }

    fprintf (output, "\n>>>>>>>>>>>>>>>    Memory Distribution    <<<<<<<<<<<<<<<\n");
    for(auto tid_and_values : mem_alloc_real_using_record) {

        pid_t tid = tid_and_values.getKey();
        uint64_t* the_mem_alloc_real_using_record = tid_and_values.getData();
        uint64_t* the_mem_alloc_wasted_record;
        uint64_t* the_mem_freelist_wasted_record;
        mem_alloc_wasted_record.find(tid, &the_mem_alloc_wasted_record);
        mem_freelist_wasted_record.find(tid, &the_mem_freelist_wasted_record);

        fprintf(output, "----tid = %10d----\n", tid);
        if(bibop) {
            for (int i = 0; i < num_class_sizes; ++i)
                fprintf(output, "idx: %10d, real: %10lu, alloc wasted: %10u, freelist wasted: %10u\n",
                        i, the_mem_alloc_real_using_record[i],
                        the_mem_alloc_wasted_record[i], the_mem_freelist_wasted_record[i]);
        } else {
            for(int i = 0; i < 2; ++i)
                fprintf(output, "idx: %10d, real: %10lu, alloc wasted: %10u, freelist wasted: %10u\n",
                        i, the_mem_alloc_real_using_record[i],
                        the_mem_alloc_wasted_record[i], the_mem_freelist_wasted_record[i]);
        }
    }
}

void MemoryWaste::reportMemory(FILE * output) {

    if(output == nullptr) {
        output = stderr;
    }

    fprintf (output, "\n>>>>>>>>>>>>>>>    Memory Distribution    <<<<<<<<<<<<<<<\n");
    for(auto tid_and_values : mem_alloc_real_using) {

        pid_t tid = tid_and_values.getKey();
        std::atomic<uint64_t>* the_mem_alloc_real_using = tid_and_values.getData();
        std::atomic<uint64_t>* the_mem_alloc_wasted;
        std::atomic<uint64_t>* the_mem_freelist_wasted;
        mem_alloc_wasted.find(tid, &the_mem_alloc_wasted);
        mem_freelist_wasted.find(tid, &the_mem_freelist_wasted);

        fprintf(output, "----tid = %10d----\n", tid);
        if(bibop) {
            for (int i = 0; i < num_class_sizes; ++i)
                fprintf(output, "idx: %10d, real: %10lu, alloc wasted: %10u, freelist wasted: %10u\n",
                        i, (uint64_t) the_mem_alloc_real_using[i],
                        (uint64_t)the_mem_alloc_wasted[i], (uint64_t)the_mem_freelist_wasted[i]);
        } else {
            for(int i = 0; i < 2; ++i)
                fprintf(output, "idx: %10d, real: %10lu, alloc wasted: %10u, freelist wasted: %10u\n",
                        i, (uint64_t) the_mem_alloc_real_using[i],
                        (uint64_t)the_mem_alloc_wasted[i], (uint64_t)the_mem_freelist_wasted[i]);
        }
    }
}