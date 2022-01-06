#ifndef __SHADOWMAP_H__
#define __SHADOWMAP_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cstddef>
#include <malloc.h>
#include <assert.h>

#include "libmallocprof.h"
#include "spinlock.hh"
#include "threadlocalstatus.h"
#include "memsample.h"
#include "definevalues.h"

#define NUM_CACHELINES_PER_PAGE_HUGE 32768

#define NUM_PAGES_PER_MEGABYTE 256
#define NUM_PAGES_PER_TWO_MEGABYTE_HUGE 1

#define CACHELINES_PER_PAGE_MASK (NUM_CACHELINES_PER_PAGE - 1)
#define CACHELINES_PER_PAGE_MASK_HUGE (NUM_CACHELINES_PER_PAGE_HUGE - 1)

#define CACHELINE_SIZE_MASK (CACHELINE_SIZE - 1)

#define PAGESIZE_MASK (PAGESIZE - 1)
#define PAGESIZE_MASK_HUGE (PAGESIZE_HUGE - 1)

#define NUM_PAGES_PER_MEGABYTE_MASK (NUM_PAGES_PER_MEGABYTE - 1)
#define NUM_PAGES_PER_TWO_MEGABYTE_MASK_HUGE 0

#define LOG2_NUM_CACHELINES_PER_PAGE 6
#define LOG2_NUM_CACHELINES_PER_PAGE_HUGE 15

#define LOG2_NUM_PAGES_PER_MEGABYTE 8
#define LOG2_NUM_PAGES_PER_TWO_MEGABYTE_HUGE 0

#define LOG2_PAGESIZE 12
#define LOG2_PAGESIZE_HUGE 21

#define LOG2_CACHELINE_SIZE 6

#define PAGE_MAP_START ((uintptr_t)0x10000000)
#define CACHE_MAP_START (PAGE_MAP_START + PAGE_MAP_SIZE)
#define OBJ_SIZE_MAP_START (CACHE_MAP_START + CACHE_MAP_SIZE)

//#define NUM_ADDRESS_RANGE 2
#define NUM_ADDRESS_RANGE 8


#define PAGE_MAP_SIZE (128 * ONE_GB)
#define CACHE_MAP_SIZE (64 * ONE_GB)

#ifdef UTIL
#define MAX_PAGE_MAP_ENTRIES (PAGE_MAP_SIZE / sizeof(PageMapEntry))
#define MAX_CACHE_MAP_ENTRIES (CACHE_MAP_SIZE / sizeof(CacheMapEntry))
#else
#define MAX_PAGE_MAP_ENTRIES 0
#define MAX_CACHE_MAP_ENTRIES 0
#endif

//#define NUM_COHERENCY_CACHES 20000

extern char * allocator_name;

inline size_t alignup(size_t size, size_t alignto) {
		return (size % alignto == 0) ? size : ((size + (alignto - 1)) & ~(alignto - 1));
}
inline void * alignupPointer(void * ptr, size_t alignto) {
  return ((intptr_t)ptr%alignto == 0) ? ptr : (void *)(((intptr_t)ptr + (alignto - 1)) & ~(alignto - 1));
}

//struct HashLocksSetForCoherency {
//    spinlock locks[NUM_COHERENCY_CACHES];
//    void lock(uint64_t index);
//    void unlock(uint64_t index);
//};

class CacheMapEntry {

public:

#ifdef CACHE_UTIL
    bool addedConflict;
    bool addedCoherency;
    int8_t num_used_bytes;
    uint8_t misses;

    void addUsedBytes(uint8_t num_bytes);
    void subUsedBytes(uint8_t num_bytes);
    void setFull();
    void setEmpty();
    uint8_t getUsedBytes();
#endif

};

#ifdef UTIL
class PageMapEntry {
public:

    bool touched = false;
    short num_used_bytes;

#ifdef CACHE_UTIL
    CacheMapEntry * cache_map_entry;

    static void mallocUpdateCacheLines(uint8_t range, uint64_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, unsigned int size);
    static void freeUpdateCacheLines(uint8_t range, uint64_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, unsigned int size, uint16_t tid);
    CacheMapEntry * getCacheMapEntry(bool mvBumpPtr = true);
#endif

    void clear();
    bool isTouched();
    void setTouched();
    unsigned short getUsedBytes();
    void addUsedBytes(unsigned short num_bytes);
    void subUsedBytes(unsigned short num_bytes);
    void setEmpty();
    void setFull();
};
#endif

class ShadowMemory {
private:

#ifdef UTIL
    static void mallocUpdatePages(uintptr_t uintaddr, uint8_t range, uint64_t page_index, int64_t size);
    static void freeUpdatePages(uintptr_t uintaddr, uint8_t range, uint64_t page_index, int64_t size);
#endif

//    static HashLocksSetForCoherency hashLocksSetForCoherency;

#ifdef UTIL
    static PageMapEntry * page_map_begin[NUM_ADDRESS_RANGE];
    static PageMapEntry * page_map_end[NUM_ADDRESS_RANGE];

#ifdef CACHE_UTIL
    static CacheMapEntry * cache_map_begin;
    static CacheMapEntry * cache_map_end;
    static CacheMapEntry * cache_map_bump_ptr;
#endif

#endif

    static bool isInitialized;

public:

    static void * addressRanges[NUM_ADDRESS_RANGE];

#ifdef UTIL

#ifdef CACHE_UTIL
    static spinlock cache_map_lock;
#endif

#endif
#ifdef OPEN_SAMPLING_EVENT
    static void doMemoryAccess(uintptr_t uintaddr, eMemAccessType accessType, bool miss);
#endif
    static bool initialize();

#ifdef UTIL

    static void cleanupPages(uintptr_t uintaddr, size_t length);

#ifdef CACHE_UTIL
    static CacheMapEntry * doCacheMapBumpPointer();
#endif

    static PageMapEntry * doPageMapBumpPointer();

#endif

    static void getPageIndex(uint64_t addr, uint8_t * range, uint64_t * index);

#ifdef UTIL
    static PageMapEntry * getPageMapEntry(uint8_t range, uint64_t page_idx);
    static void mallocUpdateObject(void * address, unsigned int size);
    static void freeUpdateObject(void * address, unsigned int size, uint16_t tid);
#endif

    static void printOutput();

};

struct CoherencyData {
    uint8_t word;
    short tid;
    short allocateTid[2];
    uint16_t ts;
    uint16_t fs;
    short tidsPerWord[8][16];
    CacheMapEntry * cme;
};

#define MASK_PAGE 0xfff
#define LOG2_CACHELINE 6

struct ConflictData {
    spinlock lockForCachelines;
    uint8_t numOfCme;
    uint8_t setOfCme[32];
    uint8_t numOfCmePerSet[64];
    uint8_t objNumPerSet[64];
    uint16_t totalMisses;
    uint16_t missesPerSet[64];
    CacheMapEntry * cme[32];

    void addMiss(uint64_t addr, CacheMapEntry * cme) {
        uint8_t setId = (uint8_t)(((uint64_t)addr & MASK_PAGE) >> LOG2_CACHELINE);
        totalMisses++;
        missesPerSet[setId]++;
        if(totalMisses >= 100 && missesPerSet[setId] * 40 >= totalMisses && !cme->addedConflict && !cme->addedCoherency) {
            lockForCachelines.lock();
            if(numOfCmePerSet[setId] < 4 && !cme->addedConflict) {
                this->cme[numOfCme] = cme;
                setOfCme[numOfCme] = setId;
                numOfCme++;
                assert(numOfCme < 32);
                numOfCmePerSet[setId]++;
                cme->addedConflict = true;
            }
            lockForCachelines.unlock();
        }
    }

    void checkObj(uint16_t tid, CacheMapEntry * cme) {
        uint8_t i;
        for(i = 0; i < numOfCme && this->cme[i] != cme; ++i) {} /// find index
        objNumPerSet[setOfCme[i]]++;
    }

    void printOutput() {
        if(totalMisses >= 100) {
            for(uint8_t i = 0; i < numOfCme; ++i) {
                if(!cme[i]->addedCoherency && missesPerSet[setOfCme[i]] * 40 >= totalMisses) {
                    if(objNumPerSet[i] >= 2) {
                        fprintf(ProgramStatus::outputFile, "\nset %u: %u%% Allocator Conflict Misses\n", i, missesPerSet[setOfCme[i]] * 100 / totalMisses);
                    } else {
                        fprintf(ProgramStatus::outputFile, "\nset %u: %u%% Application Conflict Misses\n", i, missesPerSet[setOfCme[i]] * 100 / totalMisses);
                    }
                    missesPerSet[setOfCme[i]] = 0;
                }
            }
        }
    }
};
#endif // __SHADOWMAP_H__
