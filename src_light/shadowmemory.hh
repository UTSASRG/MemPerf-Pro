#ifndef __SHADOWMAP_H__
#define __SHADOWMAP_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cstddef>
#include <malloc.h>
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

#define PAGE_MAP_SIZE (256 * ONE_GB)
#define CACHE_MAP_SIZE (64 * ONE_GB)

#define MAX_PAGE_MAP_ENTRIES (PAGE_MAP_SIZE / sizeof(PageMapEntry))
#define MAX_CACHE_MAP_ENTRIES (CACHE_MAP_SIZE / sizeof(CacheMapEntry))

extern char * allocator_name;

inline size_t alignup(size_t size, size_t alignto) {
		return (size % alignto == 0) ? size : ((size + (alignto - 1)) & ~(alignto - 1));
}
inline void * alignupPointer(void * ptr, size_t alignto) {
  return ((intptr_t)ptr%alignto == 0) ? ptr : (void *)(((intptr_t)ptr + (alignto - 1)) & ~(alignto - 1));
}

class CacheMapEntry {

public:

#ifdef CACHE_UTIL
    int8_t num_used_bytes;

    void addUsedBytes(uint8_t num_bytes);
    void subUsedBytes(uint8_t num_bytes);
    void setFull();
    void setEmpty();
    uint8_t getUsedBytes();
    void updateCache(bool isFree, uint8_t num_bytes);
#endif

};

class PageMapEntry {
public:

    bool touched = false;
    short num_used_bytes;

#ifdef CACHE_UTIL
    CacheMapEntry * cache_map_entry;

    static void updateCacheLines(uint64_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, unsigned int size, bool isFree);
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

class ShadowMemory {
private:
    static void updatePages(uintptr_t uintaddr, uint64_t page_index, int64_t size, bool isFree);

    static PageMapEntry * page_map_begin;
    static PageMapEntry * page_map_end;
    static PageMapEntry * page_map_bump_ptr;

#ifdef CACHE_UTIL
    static CacheMapEntry * cache_map_begin;
    static CacheMapEntry * cache_map_end;
    static CacheMapEntry * cache_map_bump_ptr;
#endif

    static bool isInitialized;

public:

#ifdef CACHE_UTIL
    static spinlock cache_map_lock;
#endif

    static void doMemoryAccess(uintptr_t uintaddr, eMemAccessType accessType);
    static bool initialize();
    static void cleanupPages(uintptr_t uintaddr, size_t length);

#ifdef CACHE_UTIL
    static CacheMapEntry * doCacheMapBumpPointer();
#endif

    static PageMapEntry * doPageMapBumpPointer();
    static uint64_t getPageIndex(uint64_t addr);
    static PageMapEntry * getPageMapEntry(uint64_t page_idx);
    static void updateObject(void * address, unsigned int size, bool isFree);
};

#endif // __SHADOWMAP_H__
