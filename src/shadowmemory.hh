#ifndef __SHADOWMAP_H__
#define __SHADOWMAP_H__

//#include <atomic>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cstddef>
#include <malloc.h>
#include "libmallocprof.h"

#define PAGESIZE 4096
#define NUM_CACHELINES_PER_PAGE 64
#define NUM_PAGES_PER_MEGABYTE 256
#define CACHELINES_PER_PAGE_MASK (NUM_CACHELINES_PER_PAGE - 1)
#define CACHELINE_SIZE_MASK (CACHELINE_SIZE - 1)
#define PAGESIZE_MASK (PAGESIZE - 1)
#define MEGABYTE_MASK (ONE_MEGABYTE - 1)
#define NUM_PAGES_PER_MEGABYTE_MASK (NUM_PAGES_PER_MEGABYTE - 1)
#define NUM_MEGABYTE_MAP_ENTRIES (1 << 27)
#define LOG2_NUM_CACHELINES_PER_PAGE 6
#define LOG2_NUM_PAGES_PER_MEGABYTE 8
#define LOG2_MEGABYTE_SIZE 20
#define LOG2_PAGESIZE 12
#define LOG2_CACHELINE_SIZE 6
#define LOG2_DWORD_SIZE 4
#define LOG2_WORD_SIZE 3
#define ONE_MEGABYTE 0x100000l
#define ONE_GIGABYTE 0x40000000l
#define ONE_TERABYTE 0x10000000000l
//#define MEGABYTE_MAP_START ((uintptr_t *)(1l << 47))		// this addr doesn't work, not addressable by userspace
#define MEGABYTE_MAP_START ((uintptr_t)0x10000000)
#define MEGABYTE_MAP_SIZE ONE_GIGABYTE
#define PAGE_MAP_START (MEGABYTE_MAP_START + MEGABYTE_MAP_SIZE)
#define CACHE_MAP_START (PAGE_MAP_START + PAGE_MAP_SIZE)
#define OBJ_SIZE_MAP_START (CACHE_MAP_START + CACHE_MAP_SIZE)
#define PAGE_MAP_SIZE (64 * ONE_GIGABYTE)
#define CACHE_MAP_SIZE (64 * ONE_GIGABYTE)
#define OBJ_SIZE_MAP_SIZE (64 * ONE_GIGABYTE)
#define MAX_PAGE_MAP_ENTRIES (PAGE_MAP_SIZE / sizeof(PageMapEntry))
#define MAX_CACHE_MAP_ENTRIES (CACHE_MAP_SIZE / sizeof(CacheMapEntry))
#define LIBC_MIN_OBJECT_SIZE 24
#define LIBC_METADATA_SIZE 8
#define OBJECT_SIZE_SENTINEL_SIZE 4
#define OBJECT_SIZE_SENTINEL 0xbaa80000				// 0x1755 << 19
#define OBJECT_SIZE_SENTINEL_MASK 0xfff80000	// 0x1fff << 19
#define OBJECT_SIZE_MASK 0x7ffff
#define MAX_OBJECT_SIZE 524287								// 2^19 - 1

// Located in libmallocprof.cpp globals
extern bool bibop;
extern bool isLibc;
extern char * allocator_name;
extern size_t large_object_threshold;
extern thread_local thread_data thrData;
extern thread_local PerfAppFriendly friendliness;

typedef struct {
		unsigned long mega_index;
		unsigned page_index;
		unsigned cache_index;
} map_tuple;

typedef enum {
    E_MAP_INIT_NOT = 0,
    E_MAP_INIT_WORKING,
    E_MAP_INIT_DONE,
} eMapInitStatus;

inline const char * boolToStr(bool p);
inline size_t alignup(size_t size, size_t alignto) {
		return (size % alignto == 0) ? size : ((size + (alignto - 1)) & ~(alignto - 1));
}
inline void * alignupPointer(void * ptr, size_t alignto) {
  return ((intptr_t)ptr%alignto == 0) ? ptr : (void *)(((intptr_t)ptr + (alignto - 1)) & ~(alignto - 1));
}


class CacheMapEntry {
		private:
                short num_used_bytes;

		public:
    short status = 0;
    int last_allocate = -1;
    int remain_size = -1;
    bool freed = false;
    int last_write = -1;
    bool sampled = false;
    int FS_sampled = 0;
				unsigned int getUsedBytes();
				bool addUsedBytes(unsigned int num_bytes);
				bool subUsedBytes(unsigned int num_bytes);
};

class PageMapEntry {
		private:
				bool touched;
                short num_used_bytes;
				CacheMapEntry * cache_map_entry;

		public:
				static bool updateCacheLines(uintptr_t uintaddr, unsigned long mega_index, unsigned page_index, unsigned size, bool isFree);
				static CacheMapEntry * getCacheMapEntry(map_tuple tuple);
				static CacheMapEntry * getCacheMapEntry(unsigned long mega_idx, unsigned page_idx, unsigned cache_idx);
				CacheMapEntry * getCacheMapEntry(bool mvBumpPtr = true);
				void clear();
				bool isTouched();
				void setTouched();
				unsigned int getUsedBytes();
				bool addUsedBytes(unsigned int num_bytes);
				bool subUsedBytes(unsigned int num_bytes);
};

class ShadowMemory {
		private:
				static unsigned updatePages(uintptr_t uintaddr, unsigned long mega_index, unsigned page_index, unsigned size, bool isFree);

				static PageMapEntry ** mega_map_begin;
				static PageMapEntry * page_map_begin;
				static PageMapEntry * page_map_end;
				static PageMapEntry * page_map_bump_ptr;
				static CacheMapEntry * cache_map_begin;
				static CacheMapEntry * cache_map_end;
				static CacheMapEntry * cache_map_bump_ptr;
				static pthread_spinlock_t mega_map_lock;
				static eMapInitStatus isInitialized;

		public:
				static pthread_spinlock_t cache_map_lock;
				static void doMemoryAccess(uintptr_t uintaddr, eMemAccessType accessType);
				static bool initialize();
				static inline PageMapEntry ** getMegaMapEntry(unsigned long mega_index);
				static unsigned cleanupPages(uintptr_t uintaddr, size_t length);
				static CacheMapEntry * doCacheMapBumpPointer();
				static PageMapEntry * doPageMapBumpPointer();
				static PageMapEntry * getPageMapEntry(unsigned long mega_idx, unsigned page_idx);
				static unsigned updateObject(void * address, size_t size, bool isFree);
				static map_tuple getMapTupleByAddress(uintptr_t uintaddr);
};

#endif // __SHADOWMAP_H__
