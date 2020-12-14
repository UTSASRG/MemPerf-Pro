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

#define NUM_CACHELINES_PER_PAGE 64
#define NUM_CACHELINES_PER_PAGE_HUGE 32768

#define NUM_PAGES_PER_MEGABYTE 256
#define NUM_PAGES_PER_TWO_MEGABYTE_HUGE 1

#define CACHELINES_PER_PAGE_MASK (NUM_CACHELINES_PER_PAGE - 1)
#define CACHELINES_PER_PAGE_MASK_HUGE (NUM_CACHELINES_PER_PAGE_HUGE - 1)

#define CACHELINE_SIZE_MASK (CACHELINE_SIZE - 1)

#define PAGESIZE_MASK (PAGESIZE - 1)
#define PAGESIZE_MASK_HUGE (PAGESIZE_HUGE - 1)

#define MEGABYTE_MASK (ONE_MB - 1)
#define TWO_MEGABYTE_MASK_HUGE (2 * ONE_MB - 1)

#define NUM_PAGES_PER_MEGABYTE_MASK (NUM_PAGES_PER_MEGABYTE - 1)
#define NUM_PAGES_PER_TWO_MEGABYTE_MASK_HUGE 0

#define NUM_MEGABYTE_MAP_ENTRIES (1 << 27)
#define NUM_TWO_MEGABYTE_MAP_ENTRIES_HUGE (1 << 26)

#define LOG2_NUM_CACHELINES_PER_PAGE 6
#define LOG2_NUM_CACHELINES_PER_PAGE_HUGE 15

#define LOG2_NUM_PAGES_PER_MEGABYTE 8
#define LOG2_NUM_PAGES_PER_TWO_MEGABYTE_HUGE 0

#define LOG2_MEGABYTE_SIZE 20
#define LOG2_TWO_MEGABYTE_SIZE_HUGE 21

#define LOG2_PAGESIZE 12
#define LOG2_PAGESIZE_HUGE 21


#define LOG2_CACHELINE_SIZE 6

#define MEGABYTE_MAP_START ((uintptr_t)0x10000000)
#define MEGABYTE_MAP_SIZE ONE_GB
#define PAGE_MAP_START (MEGABYTE_MAP_START + MEGABYTE_MAP_SIZE)
#define CACHE_MAP_START (PAGE_MAP_START + PAGE_MAP_SIZE)
#define OBJ_SIZE_MAP_START (CACHE_MAP_START + CACHE_MAP_SIZE)
#define PAGE_MAP_SIZE (64 * ONE_GB)
//#define PAGE_MAP_SIZE (256 * ONE_GB)
#define CACHE_MAP_SIZE (64 * ONE_GB)
#define OBJ_SIZE_MAP_SIZE (64 * ONE_GB)
#define MAX_PAGE_MAP_ENTRIES (PAGE_MAP_SIZE / sizeof(PageMapEntry))
#define MAX_CACHE_MAP_ENTRIES (CACHE_MAP_SIZE / sizeof(CacheMapEntry))

// Located in libmallocprof.cpp globals
extern char * allocator_name;

typedef struct {
		unsigned page_index;
		unsigned cache_index;
        unsigned long mega_index;
} map_tuple;


inline const char * boolToStr(bool p);
inline size_t alignup(size_t size, size_t alignto) {
		return (size % alignto == 0) ? size : ((size + (alignto - 1)) & ~(alignto - 1));
}
inline void * alignupPointer(void * ptr, size_t alignto) {
  return ((intptr_t)ptr%alignto == 0) ? ptr : (void *)(((intptr_t)ptr + (alignto - 1)) & ~(alignto - 1));
}

#ifdef ENABLE_PRECISE_BLOWUP
struct BlowupNode {
    short blowupFlag;
    short freelistNum;
    unsigned short classSizeIndex;
    BlowupNode * next;
};
#endif

#ifdef ENABLE_HP
struct RangeOfHugePages {
    unsigned num;
    uintptr_t retvals[1000];
    size_t lengths[1000];
    void add(uintptr_t retval, size_t length);
};
#endif

class CacheMapEntry {
		public:

//    bool sampled = false;
//    bool allocated = false;
    bool falseSharingStatus[2] = {false, false};
    bool falseSharingLineRecorded[NUM_OF_FALSESHARINGTYPE] = {false};
//    FalseSharingType falseSharingStatus = OBJECT;
    int8_t num_used_bytes;
//    short lastWriterThreadIndex = -1;
//    short lastAFThreadIndex = -1;
    uint8_t lastWriterThreadIndex;
    uint8_t lastAFThreadIndex;

    uint8_t getUsedBytes();
    void addUsedBytes(uint8_t num_bytes);
    void subUsedBytes(uint8_t num_bytes);
    void updateCache(bool isFree, uint8_t num_bytes);
    void setFull();
    void setEmpty();

    void setFS(FalseSharingType falseSharingType);
    FalseSharingType getFS();
    static uint8_t getThreadIndex();
};

class PageMapEntry {
public:

    bool donatedBySyscall;
#ifdef ENABLE_HP
    bool hugePage;
#endif
    bool touched = false;
    short num_used_bytes;
    CacheMapEntry * cache_map_entry;
#ifdef ENABLE_PRECISE_BLOWUP
    BlowupNode * blowupList;
    spinlock pagelock;
#endif
    static void updateCacheLines(unsigned long mega_index, uint8_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, unsigned int size, bool isFree);
    CacheMapEntry * getCacheMapEntry(bool mvBumpPtr = true);
    void clear();
    bool isTouched();
    void setTouched();
    unsigned short getUsedBytes();
    void addUsedBytes(unsigned short num_bytes);
    void subUsedBytes(unsigned short num_bytes);
    void setEmpty();
    void setFull();

#ifdef ENABLE_PRECISE_BLOWUP
    BlowupNode * newBlowup(unsigned int classSizeIndex);
    void addBlowup(unsigned int classSizeIndex);
    void subBlowup(unsigned int classSizeIndex);
    void addFreeListNum(unsigned int classSizeIndex);
    void subFreeListNum(unsigned int classSizeIndex);
    void clearBlowup();
#endif
};

class ShadowMemory {
private:
    static unsigned int updatePages(uintptr_t uintaddr, unsigned long mega_index, uint8_t page_index, int64_t size, bool isFree);

    static PageMapEntry ** mega_map_begin;
    static PageMapEntry * page_map_begin;
    static PageMapEntry * page_map_end;
    static PageMapEntry * page_map_bump_ptr;
    static CacheMapEntry * cache_map_begin;
    static CacheMapEntry * cache_map_end;
    static CacheMapEntry * cache_map_bump_ptr;
    static spinlock mega_map_lock;
    static bool isInitialized;
#ifdef PRINT_MEM_DETAIL_THRESHOLD
    static void * maxAddress;
    static void * minAddress;
#endif

public:
#ifdef ENABLE_HP
    static RangeOfHugePages HPBeforeInit;
#ifdef ENABLE_THP
    static RangeOfHugePages THPBeforeInit;
#endif
#endif
    static spinlock cache_map_lock;
    static void doMemoryAccess(uintptr_t uintaddr, eMemAccessType accessType);
    static bool initialize();
    static inline PageMapEntry ** getMegaMapEntry(unsigned long mega_index);
#ifdef ENABLE_HP
    static void setHugePages(uintptr_t uintaddr, size_t length);
    static void cancelHugePages(uintptr_t uintaddr, size_t length);
#ifdef ENABLE_THP
    static void setTransparentHugePages(uintptr_t uintaddr, size_t length);
#endif
#endif
    static size_t cleanupPages(uintptr_t uintaddr, size_t length);
    static CacheMapEntry * doCacheMapBumpPointer();
    static PageMapEntry * doPageMapBumpPointer();
    static PageMapEntry * getPageMapEntry(void * address);
    static PageMapEntry * getPageMapEntry(unsigned long mega_idx, unsigned page_idx);
    static unsigned int updateObject(void * address, unsigned int size, bool isFree);
    static map_tuple getMapTupleByAddress(uintptr_t uintaddr);
#ifdef ENABLE_HP
    static void setHugePagesInit();
    #ifdef ENABLE_THP
    static void setTransparentHugePagesInit();
    #endif
    static bool inHPInitRange(void * address);
#endif

#ifdef ENABLE_PRECISE_BLOWUP
    static void addBlowup(void * address, unsigned int classSizeIndex);
    static void subBlowup(void * address, unsigned int classSizeIndex);
    static void addFreeListNum(void *address, unsigned int classSizeIndex);
    static void subFreeListNum(void *address, unsigned int classSizeIndex);
#endif
#ifdef PRINT_MEM_DETAIL_THRESHOLD
    static void printAddressRange();
    static void printAllPages();
#endif
};

#endif // __SHADOWMAP_H__
