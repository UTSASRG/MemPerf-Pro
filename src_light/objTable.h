
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

class ProgramStatus;


struct HashLocksSet {
    spinlock locks[MAX_OBJ_NUM];
    void lock(void * address);
    void unlock(void * address);
};

class ObjTable{
public:

    static HashLocksSet hashLocksSet;

    static void initialize();

    static bool allocUpdate(unsigned int size, void * address);
    static uint32_t freeUpdate(void* address);

};

#endif //MMPROF_OBJTABLE_H
