#ifndef __SHADOWMAP_H__
#define __SHADOWMAP_H__

#include <stdint.h>
#include <stdlib.h>
#include <cstddef>

#define num_cache_lines(X) (X & SHADOW_MEM_CACHE_UTIL_MASK ? \
				((X >> SHADOW_MEM_CACHE_UTIL_BITS) + 1) : \
				(X >> SHADOW_MEM_CACHE_UTIL_BITS))

class ShadowMemory {
		private:
				inline static size_t alignup(size_t size, size_t alignto) {
						return (size % alignto == 0) ? size : ((size + (alignto - 1)) & ~(alignto - 1));
				}

				const static short CACHE_LINE_SIZE = 8;
				const static short CACHE_LINE_SIZE_BITS = 64;
				const static short CACHE_LINE_SIZE_BITS_EXP = 7;
				const static short SHADOW_MEM_ENTRY_BITS = 32;
				const static short SHADOW_MEM_CACHE_UTIL_BITS = 7;
				const static short SHADOW_MEM_CACHE_UTIL_MASK = (CACHE_LINE_SIZE - 1);
				const static short SHADOW_MEM_OBJ_SIZE_BITS = (SHADOW_MEM_ENTRY_BITS - SHADOW_MEM_CACHE_UTIL_BITS);
				const static short PAGESIZE = 4096;
				const static uint32_t SHADOW_MEM_ERROR = 0xffffffff;

				size_t map_length;
				void * map_begin;
				void * map_end;
				uint32_t * shadow_map;

		public:
				ShadowMemory();
				ShadowMemory(void * address);
				int compare(ShadowMemory * other);
				bool contains(void * address);
				void initialize(void * address, size_t size);
				size_t getMappingLength() { return map_length; }
				void * getMappingBegin() { return map_begin; }
				inline uint32_t * getObjectMetadataAddr(void * address);
				inline uint32_t getObjectMetadata(void * address);
				uint32_t * getShadowBegin() { return shadow_map; }
				size_t getObjectSize(void * addr);
				int getCacheLineUsage(void * address);
				void updateObject(void * address, size_t size);
};

#endif // __SHADOWMAP_H__
