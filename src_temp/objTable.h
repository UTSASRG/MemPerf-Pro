
#ifndef MMPROF_OBJTABLE_H
#define MMPROF_OBJTABLE_H

#include <stdio.h>
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "libmallocprof.h"
#include "memsample.h"
#include "mymalloc.h"
#include "threadlocalstatus.h"
#include "allocatingstatus.h"
#include "structs.h"
#include "callsite.h"

#ifndef MEMORY_WASTE

class ProgramStatus;


struct HashLocksSet {
    spinlock locks[MAX_OBJ_NUM];
    void lock(void * address);
    void unlock(void * address);
};

struct ObjStat {
    uint8_t callKey;
    uint16_t tid;
    uint32_t size;

    static ObjStat newObj(uint8_t callKey, uint16_t tid, uint32_t size) {
        ObjStat obj;
        obj.callKey = callKey;
        obj.tid = tid;
        obj.size = size;
        return obj;
    }
};

class ObjTable{
public:

    static HashLocksSet hashLocksSet;

    static void initialize();

    static bool allocUpdate(unsigned int size, void * address);
    static ObjStat * freeUpdate(void* address);

};

#endif

#endif //MMPROF_OBJTABLE_H