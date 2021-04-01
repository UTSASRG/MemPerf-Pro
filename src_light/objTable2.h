
#ifndef SRC_LIGHT_OBJTABLE2_H
#define SRC_LIGHT_OBJTABLE2_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "libmallocprof.h"
#include "memsample.h"
#include "mymalloc.h"
#include "threadlocalstatus.h"
#include "allocatingstatus.h"
#include "structs.h"
#include "definevalues.h"

///12 -> 4K; 16 -> 64K; 20 -> 1M
//#define CHUNK_SIZE ONE_MB
//#define LOG2_CHUNK 20
//#define MASK_CHUNK (CHUNK_SIZE-1)
//#define MAX_CHUNK_OBJ 4096
//#define NUM_CHUNK 4096

#define CHUNK_SIZE 4*ONE_KB
#define LOG2_CHUNK 12
#define MASK_CHUNK (CHUNK_SIZE-1)
#define MAX_CHUNK_OBJ 512
#define NUM_CHUNK 262144*8

struct HashLocksSet {
    spinlock locks[NUM_CHUNK];
    void lock(uint64_t chunkId);
    void unlock(uint64_t chunkId);
};


class ObjTable {

public:
    static HashMap<uint64_t, HashMap<uint32_t, uint32_t, PrivateHeap>, PrivateHeap> chunkTable;
    static HashLocksSet hashLocksSet;

    static inline uint64_t getChunkId(void * addr);
    static inline uint32_t getChunkOffset(void * addr);
    static void initialize();
    static bool allocUpdate(uint32_t size, void * address);
    static uint32_t freeUpdate(void * address);
};

#endif //SRC_LIGHT_OBJTABLE2_H
