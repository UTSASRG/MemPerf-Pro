#ifndef __SHADOWMAP_H__
#define __SHADOWMAP_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cstddef>
#include <malloc.h>
#include <assert.h>
#include <atomic>

#include "libmallocprof.h"
#include "spinlock.hh"
#include "threadlocalstatus.h"
#include "memsample.h"
#include "definevalues.h"
#include "callsite.h"
#include "objTable.h"

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

//#ifdef UTIL
#define MAX_PAGE_MAP_ENTRIES (PAGE_MAP_SIZE / sizeof(PageMapEntry))
#define MAX_CACHE_MAP_ENTRIES (CACHE_MAP_SIZE / sizeof(CacheMapEntry))
//#endif

struct ObjStat;
namespace Callsite {
    extern uint16_t numCallKey;

    uint8_t getCallKey(uint8_t oldCallKey);
    void * ConvertToVMA(void* addr);
    void ssystem(char * command);
    void printCallSite(uint8_t callKey);
}

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

    uint8_t misses; /// conflict = (misses & (uint8_t)0x80), coherency = (misses & (uint8_t)0x40), miss = (misses & (uint8_t)0x3f)

#ifdef CACHE_UTIL
    int8_t num_used_bytes;
    void addUsedBytes(uint8_t num_bytes);
    void subUsedBytes(uint8_t num_bytes);
    void setFull();
    void setEmpty();
    uint8_t getUsedBytes();
#endif

};


class PageMapEntry {
public:
//#ifdef UTIL
    short num_used_bytes;
//#endif
    CacheMapEntry * cache_map_entry;
#ifdef CACHE_UTIL
    static void mallocUpdateCacheLines(uint8_t range, uint64_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, unsigned int size);
#endif
    static void freeUpdateCacheLines(uint8_t range, uint64_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, ObjStat objStat);
    CacheMapEntry * getCacheMapEntry(bool mvBumpPtr = true);

#ifdef UTIL
    void clear();
#endif
    unsigned short getUsedBytes();
    void addUsedBytes(unsigned short num_bytes);
#ifdef UTIL
    void subUsedBytes(unsigned short num_bytes);
    void setEmpty();
#endif
    void setFull();
};


class ShadowMemory {
private:

    static void mallocUpdatePages(uintptr_t uintaddr, uint8_t range, uint64_t page_index, int64_t size);
#ifdef UTIL
    static void freeUpdatePages(uintptr_t uintaddr, uint8_t range, uint64_t page_index, int64_t size);
#endif

//    static HashLocksSetForCoherency hashLocksSetForCoherency;

//#ifdef UTIL
    static PageMapEntry * page_map_begin[NUM_ADDRESS_RANGE];
    static PageMapEntry * page_map_end[NUM_ADDRESS_RANGE];

//#ifdef CACHE_UTIL
    static CacheMapEntry * cache_map_begin;
    static CacheMapEntry * cache_map_end;
    static CacheMapEntry * cache_map_bump_ptr;
//#endif

//#endif

    static bool isInitialized;

public:

    static void * addressRanges[NUM_ADDRESS_RANGE];

//#ifdef UTIL

//#ifdef CACHE_UTIL
    static spinlock cache_map_lock;
//#endif

//#endif
#ifdef OPEN_SAMPLING_EVENT
    static void doMemoryAccess(uintptr_t uintaddr, eMemAccessType accessType, bool miss);
#endif
    static bool initialize();

#ifdef UTIL
    static void cleanupPages(uintptr_t uintaddr, size_t length);
#endif

//#ifdef CACHE_UTIL
    static CacheMapEntry * doCacheMapBumpPointer();
//#endif

    static PageMapEntry * doPageMapBumpPointer();



    static void getPageIndex(uint64_t addr, uint8_t * range, uint64_t * index);

    static PageMapEntry * getPageMapEntry(uint8_t range, uint64_t page_idx);
    static void mallocUpdateObject(void * address, unsigned int size);
    static void freeUpdateObject(void * address, ObjStat objStat);
    static void printOutput();

};

bool cmp(uint8_t a, uint8_t b);

#define NUM_WORD 8

struct CoherencyData {
    uint8_t callKey[2];
    uint16_t misses;
    uint16_t allocateTid[2];
    uint16_t tidsPerWord[NUM_WORD][16];
    CacheMapEntry * cme;
};

#define MASK_PAGE 0xfff
#define LOG2_CACHELINE 6
#define CME 128

struct ConflictData {
    spinlock lockForCachelines;
    uint8_t numOfCme;
    uint8_t setOfCme[CME];
    uint8_t numOfCmePerSet[64];
    uint8_t objNumPerSet[64];
    uint8_t callKeyPerSet[64][2];
    uint32_t totalMisses;
    uint32_t missesPerSet[64];
    CacheMapEntry * cme[CME];

    void addMiss(uint64_t addr, CacheMapEntry * cme) {
        uint8_t setId = (uint8_t)(((uint64_t)addr & MASK_PAGE) >> LOG2_CACHELINE);
//        lockForCachelines.lock();
        totalMisses++;
//        lockForCachelines.unlock();
        missesPerSet[setId]++;
        if(totalMisses >= 50 && missesPerSet[setId] * 20 >= totalMisses && !(cme->misses & (uint8_t)0x80) && !(cme->misses & (uint8_t)0x40)) {
            if(numOfCme < CME && numOfCmePerSet[setId] < 16 && !(cme->misses & (uint8_t)0x80)) {
                lockForCachelines.lock();
                if(numOfCme < CME && numOfCmePerSet[setId] < 16 && !(cme->misses & (uint8_t)0x80)) {
                    this->cme[numOfCme] = cme;
                    setOfCme[numOfCme] = setId;
                    numOfCme++;
                    numOfCmePerSet[setId]++;
//                    cme->addedConflict = true;
                    cme->misses |= (uint8_t)0x80;
                }
                lockForCachelines.unlock();
            }
        }
    }

    void checkObj(uint16_t tid, CacheMapEntry * cme, uint8_t callKey) {
        uint8_t i;
        for(i = 0; i < numOfCme && this->cme[i] != cme; ++i) {} /// find index
        objNumPerSet[setOfCme[i]]++;
//        fprintf(stderr, "%u %u\n", setOfCme[i], objNumPerSet[setOfCme[i]]);
        if(callKeyPerSet[setOfCme[i]][0]) {
            if(callKeyPerSet[setOfCme[i]][0] != callKey && !callKeyPerSet[setOfCme[i]][1]) {
                callKeyPerSet[setOfCme[i]][1] = callKey;
            }
        } else {
            callKeyPerSet[setOfCme[i]][0] = callKey;
        }
    }

    void printOutput() {
        fprintf(stderr, "numOfCme = %u, totalMisses = %u\n", numOfCme, totalMisses);
//        for(uint8_t i = 0; i < 64; i++) {
//            fprintf(stderr, "%u %u%%\n", i, missesPerSet[setOfCme[i]] * 100 / totalMisses);
//        }
        if(totalMisses >= 50) {
            uint8_t totalMissScore = 0;
            uint8_t scores[20];
            uint8_t numOfScores = 0;
//            for(int i = 0; i < 64; ++i) {
//                fprintf(stderr, "%u\n", missesPerSet[setOfCme[i]]);
//            }
            for(uint8_t i = 0; i < numOfCme; ++i) {
                if(cme[i]->misses && missesPerSet[setOfCme[i]] * 20 >= totalMisses) {
                    if(objNumPerSet[setOfCme[i]] >= 2) {
                        fprintf(ProgramStatus::outputFile, "\nset %u: %u%% Allocator Conflict Misses %u %u\n", setOfCme[i], missesPerSet[setOfCme[i]] * 100 / totalMisses,
                                missesPerSet[setOfCme[i]], totalMisses);
                        scores[numOfScores++] = missesPerSet[setOfCme[i]] * 100 / totalMisses;
                        missesPerSet[setOfCme[i]] = 0;
                    } else {
                        fprintf(ProgramStatus::outputFile, "\nset %u: %u%% Application Conflict Misses\n", setOfCme[i], missesPerSet[setOfCme[i]] * 100 / totalMisses);
                    }
                    if(callKeyPerSet[setOfCme[i]][0]) {
                        Callsite::printCallSite(callKeyPerSet[setOfCme[i]][0]);
                    }
                    if(callKeyPerSet[setOfCme[i]][1]) {
                        Callsite::printCallSite(callKeyPerSet[setOfCme[i]][1]);
                    }
                }
            }
            std::sort(scores, scores+numOfScores, cmp);
            for(uint64_t i = 0, j = 1; i < numOfScores; ++i, j*=2) {
//                fprintf(stderr, " %u", scores[i]);
                totalMissScore += scores[i] / j;
            }
            fprintf(ProgramStatus::outputFile, "Conflict Score = %u\n", totalMissScore);
        }
    }
};
#endif // __SHADOWMAP_H__
