#ifndef __SHADOWMAP_H__
#define __SHADOWMAP_H__

#include <stdint.h>
#include <stdlib.h>
#include <cstddef>

#define PAGESIZE 4096
#define CACHELINE_SIZE 64
#define NUM_CACHELINES_PER_PAGE 64
#define CACHELINE_SIZE_MASK (CACHELINE_SIZE - 1)
#define PAGESIZE_MASK (PAGESIZE - 1)
#define MEGABYTE_MASK (ONE_MEGABYTE - 1)
#define NUM_MEGABYTE_MAP_ENTRIES (1 << 27)
#define LOG2_NUM_CACHELINES_PER_PAGE 6
#define LOG2_MEGABYTE_SIZE 20
#define LOG2_PAGESIZE 12
#define LOG2_CACHELINE_SIZE 6
#define LOG2_PTR_SIZE 3
#define ONE_MEGABYTE 0x100000l
#define ONE_GIGABYTE 0x40000000l
#define ONE_TERABYTE 0x10000000000l
#define NUM_PAGE_MAP_ENTRIES_PER_MB 256
//#define MEGABYTE_MAP_START ((uintptr_t *)(1l << 47))		// this doesn't work, not addressable by userspace
#define MEGABYTE_MAP_START ((uintptr_t)0x100000000)
#define MEGABYTE_MAP_SIZE ONE_GIGABYTE
#define PAGE_MAP_START (MEGABYTE_MAP_START + MEGABYTE_MAP_SIZE)
#define CACHE_MAP_START (PAGE_MAP_START + PAGE_MAP_SIZE)
#define OBJ_SIZE_MAP_START (CACHE_MAP_START + CACHE_MAP_SIZE)
#define PAGE_MAP_SIZE (256 * ONE_GIGABYTE)
#define CACHE_MAP_SIZE (256 * ONE_GIGABYTE)
#define OBJ_SIZE_MAP_SIZE (256 * ONE_GIGABYTE)

typedef enum {
    E_MAP_INIT_NOT = 0,
    E_MAP_INIT_WORKING,
    E_MAP_INIT_DONE,
} eMapInitStatus;


class CacheMapEntry {
		private:
				unsigned char num_used_bytes;

		public:
				unsigned char getUsedBytes();
				unsigned char setUsedBytes(unsigned num_bytes);
				unsigned char addUsedBytes(unsigned num_bytes);
				unsigned char subUsedBytes(unsigned num_bytes);
};

class PageMapEntry {
		private:
				bool isPageMonoObj;
				unsigned short num_used_bytes;
				CacheMapEntry * cache_map_entry;
				unsigned obj_size_index;
				CacheMapEntry * getCacheMapEntry_helper();

		public:
				void initialize();
				CacheMapEntry * getCacheMapEntry(unsigned cache_idx);
				unsigned getSizeMapIndex();
				bool setUsedBytes(unsigned num_bytes);
				bool addUsedBytes(unsigned num_bytes);
				bool subUsedBytes(unsigned num_bytes);
				bool isPageMonoObject();
				void setPageMonoObject(bool status);
				bool updateCacheLines(uintptr_t address, unsigned size);
};

class ShadowMemory {
		private:
				inline static size_t alignup(size_t size, size_t alignto) {
						return (size % alignto == 0) ? size : ((size + (alignto - 1)) & ~(alignto - 1));
				}

				static PageMapEntry ** mega_map_begin;
				static PageMapEntry * page_map_begin;
				static uintptr_t * obj_size_map_begin;
				static PageMapEntry * page_map_bump_ptr;
				static uintptr_t * obj_size_map_bump_ptr;
				static uintptr_t * cache_map_begin;
				static uintptr_t * cache_map_bump_ptr;
				static eMapInitStatus isInitialized;

		public:
				static bool initialize();
				static void updateObject(void * address, size_t size);
				//unsigned getObjectSize(void * object);
				//unsigned getCacheUsage(void * address);
				//unsigned getPageUsage(void * address);
};

#endif // __SHADOWMAP_H__
