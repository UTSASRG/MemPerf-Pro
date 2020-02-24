//
// Created by 86152 on 2020/2/22.
//
#include <atomic>
#include <stdio.h>
#include "memwaste.h"

HashMap <void*, HashMap<void*, bool*, spinlock>*, spinlock> MemoryWaste::objects_each_page;
HashMap <void*, obj_status*, spinlock> MemoryWaste::addr_obj_status;
HashMap <pid_t, std::atomic<uint64_t>*, spinlock> MemoryWaste::mem_alloc_real_using;
HashMap <pid_t, std::atomic<uint64_t>*, spinlock> MemoryWaste::mem_alloc_wasted;
HashMap <pid_t, std::atomic<uint64_t>*, spinlock> MemoryWaste::mem_freelist_wasted;
std::atomic<uint64_t> * MemoryWaste::mem_never_used;

obj_status * MemoryWaste::newObjStatus(size_t remain_size, pid_t tid, size_t size_using, size_t classSize) {
    obj_status * ptr = (obj_status*) malloc(sizeof(obj_status));
    ptr->tid = tid;
    ptr->remain_size = remain_size;
    ptr->size_using = size_using;
    ptr->classSize = classSize;
    return ptr;
}

void MemoryWaste::initialize() {
    objects_each_page.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_PAGE_NUM);
    addr_obj_status.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);
    mem_alloc_real_using.initialize(HashFuncs::hashInt, HashFuncs::compareInt, MAX_THREAD_NUMBER);
    mem_alloc_wasted.initialize(HashFuncs::hashInt, HashFuncs::compareInt, MAX_THREAD_NUMBER);
    mem_freelist_wasted.initialize(HashFuncs::hashInt, HashFuncs::compareInt, MAX_THREAD_NUMBER);
    if(bibop) {
        mem_never_used = (std::atomic<uint64_t>*) myMalloc(num_class_sizes * sizeof(std::atomic<uint64_t>));
    } else {
        mem_never_used = (std::atomic<uint64_t>*) myMalloc(2 * sizeof(std::atomic<uint64_t>));
    }
}

void MemoryWaste::initForNewTid(pid_t tid) {
    std::atomic<uint64_t>* tmp_addr;
    if(bibop) {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(num_class_sizes * sizeof(std::atomic<uint64_t>));
    } else {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(num_class_sizes * sizeof(std::atomic<uint64_t>));
    }
    mem_alloc_real_using.insert(tid, tmp_addr);
    if(bibop) {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(num_class_sizes * sizeof(std::atomic<uint64_t>));
    } else {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(num_class_sizes * sizeof(std::atomic<uint64_t>));
    }
    mem_alloc_wasted.insert(tid, tmp_addr);
    if(bibop) {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(num_class_sizes * sizeof(std::atomic<uint64_t>));
    } else {
        tmp_addr = (std::atomic<uint64_t>*) myMalloc(num_class_sizes * sizeof(std::atomic<uint64_t>));
    }
    mem_freelist_wasted.insert(tid, tmp_addr);

}

void MemoryWaste::initForNewPage(void* pageidx) {
    HashMap<void*, bool*, spinlock>* addrset = new HashMap<void*, bool*, spinlock>();
    addrset->initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, PAGESIZE);
    objects_each_page.insert(pageidx, addrset);
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
    the_mem_alloc_wasted[classSizeIndex] += classSize-size;

    /* New or Reused? Get old status */
    obj_status * old_status;
    if(! addr_obj_status.find(address, &old_status)) {
        reused = false;
        /* mem_never_used */
        mem_never_used[classSizeIndex] -= classSize;
        /* new status */
        addr_obj_status.insert(address, newObjStatus(0, tid, size, classSize));
        /* objects_each_page */
        void* pageidx = (void*)((uint64_t)address ^ (uint64_t)~PAGESIZE_MASK);
        HashMap<void*, bool*, spinlock> * addrset;
        if(! objects_each_page.find(pageidx, &addrset)) {
            initForNewPage(pageidx);
        }
        if(! objects_each_page.find(pageidx, &addrset)) {
            fprintf(stderr, "objects_each_page key error: %p\n", pageidx);
            abort();
        }
        addrset->insert(address, new bool());
    } else {
        reused = true;
        std::atomic<uint64_t>* the_mem_freelist_wasted_new;
        if(! mem_freelist_wasted.find(tid, &the_mem_freelist_wasted_new)) {
            fprintf(stderr, "mem_freelist_wasted key error: %d\n", tid);
            abort();
        }
        /* freelist_wasted[oldthread] -> [newthread] */
        if(old_status->tid != tid) {
            std::atomic<uint64_t>* the_mem_freelist_wasted_old;
            if(! mem_freelist_wasted.find(old_status->tid, &the_mem_freelist_wasted_old)) {
                fprintf(stderr, "mem_freelist_wasted key error: %d\n", old_status->tid);
                abort();
            }
            short old_classSizeIndex = old_status->classSize;
            if(old_classSizeIndex != classSizeIndex) {
                fprintf(stderr, "warning: classsizeindex changed!\n");
            }
            the_mem_freelist_wasted_old[classSizeIndex] -= old_status->remain_size;
            the_mem_freelist_wasted_new[classSizeIndex] += old_status->remain_size;
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
        if (old_status->remain_size <= classSize) {
            new_status->remain_size = 0;
        } else {
            new_status->remain_size = old_status->remain_size - classSize;
        }
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
    the_mem_alloc_wasted[classSizeIndex] -= classSize - size;

    std::atomic<uint64_t>* the_mem_freelist_wasted_new;
    if(! mem_freelist_wasted.find(tid, &the_mem_freelist_wasted_new)) {
        fprintf(stderr, "mem_freelist_wasted key error: %d\n", tid);
        abort();
    }

    /* thread changed? freelist_wasted[oldthread] -> [newthread] */
    if(old_status->tid != tid) {
        std::atomic<uint64_t>* the_mem_freelist_wasted_old;
        if(! mem_freelist_wasted.find(old_status->tid, &the_mem_freelist_wasted_old)) {
            fprintf(stderr, "mem_freelist_wasted key error: %d\n", old_status->tid);
            abort();
        }
        the_mem_freelist_wasted_old[classSizeIndex] -= old_status->remain_size;
        the_mem_freelist_wasted_new[classSizeIndex] += old_status->remain_size;
    }

    /* freelist_wasted[newthread] */
    the_mem_freelist_wasted_new[classSizeIndex] += classSize;

    /* new status */
    obj_status * new_status = (obj_status*)malloc(sizeof(obj_status));
    new_status->tid = tid;
    new_status->remain_size = old_status->remain_size + classSize;
    addr_obj_status.erase(address);
    addr_obj_status.insert(address, new_status);
}

void MemoryWaste::reportMemory() {
    for(auto tid_and_values : mem_alloc_real_using) {
        std::atomic<uint64_t>* the_mem_alloc_real_using = tid_and_values.getData();
        fprintf(stderr, "---mem_alloc_real_using tid = %10d----\n", tid_and_values.getKey());
        if(bibop) {
            for (int i = 0; i < num_class_sizes; ++i)
                fprintf(stderr, "idx: %10d, value: %20lu\n", i, (uint64_t) the_mem_alloc_real_using[i]);
        } else {
            for(int i = 0; i < 2; ++i)
                fprintf(stderr, "idx: %10d, value: %20lu\n", i, (uint64_t)the_mem_alloc_real_using[i]);
        }
    }

    for(auto tid_and_values : mem_alloc_wasted) {
        std::atomic<uint64_t>* the_mem_alloc_wasted = tid_and_values.getData();
        fprintf(stderr, "---mem_alloc_wasted tid = %10d----\n", tid_and_values.getKey());
        if(bibop) {
            for (int i = 0; i < num_class_sizes; ++i)
                fprintf(stderr, "idx: %10d, value: %20lu\n", i, (uint64_t) the_mem_alloc_wasted[i]);
        } else {
            for(int i = 0; i < 2; ++i)
                fprintf(stderr, "idx: %10d, value: %20lu\n", i, (uint64_t)the_mem_alloc_wasted[i]);
        }
    }

    for(auto tid_and_values : mem_freelist_wasted) {
        std::atomic<uint64_t>* the_mem_freelist_wasted = tid_and_values.getData();
        fprintf(stderr, "---mem_freelist_wasted tid = %10d----\n", tid_and_values.getKey());
        if(bibop) {
            for (int i = 0; i < num_class_sizes; ++i)
                fprintf(stderr, "idx: %10d, value: %20lu\n", i, (uint64_t) the_mem_freelist_wasted[i]);
        } else {
            for(int i = 0; i < 2; ++i)
                fprintf(stderr, "idx: %10d, value: %20lu\n", i, (uint64_t)the_mem_freelist_wasted[i]);
        }
    }

    fprintf(stderr, "---mem_never_used----\n");
    if(bibop) {
        for (int i = 0; i < num_class_sizes; ++i)
            fprintf(stderr, "idx: %10d, value: %20lu\n", i, (uint64_t) mem_never_used[i]);
    } else {
        for(int i = 0; i < 2; ++i)
            fprintf(stderr, "idx: %10d, value: %20lu\n", i, (uint64_t)mem_never_used[i]);
    }
}