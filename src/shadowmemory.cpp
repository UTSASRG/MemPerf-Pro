#include "shadowmemory.hh"

#include <malloc.h>
#include <sys/mman.h>

#include "real.hh"

#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>

ShadowMemory::ShadowMemory() {}

// Creates an "anonymous" (not really) object that will be used for finding a given
// object address in the red-black tree of ShadowMemory objects
ShadowMemory::ShadowMemory(void * address) : map_begin(address) {}

// Initializes this shadow memory object based on the start address and length of
// a heap segment mapping
void ShadowMemory::initialize(void * address, size_t size) {
		map_length = alignup(size, PAGESIZE);
    map_begin = address;
    map_end = (void *)((char *)address + map_length);

    shadow_map = (uint32_t *)RealX::mmap(NULL, ((alignup(size, CACHE_LINE_SIZE) >> 1)),
						PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
		if(shadow_map == MAP_FAILED) {
				perror("mmap of shadow memory region failed");
				abort();
		}
		//fprintf(stderr, "> created sm region %p (length = %zu) for mmap mapping at %p (length = %zu)\n",
		//			shadow_map, ((alignup(size, CACHE_LINE_SIZE) >> 1)), address, size);
}

// Returns the metadata for the object that contains the given address
inline uint32_t * ShadowMemory::getObjectMetadataAddr(void * address) {
		if(!address) {
				return NULL;
		}
		ptrdiff_t offset = (uintptr_t)address - (uintptr_t)getMappingBegin();
		unsigned index = offset >> CACHE_LINE_SIZE_BITS_EXP;
		uint32_t *retval = &shadow_map[index];
		//if(retval > (uint32_t *)0x7fffffffffff) {
		//		fprintf(stderr, "WARNING: this object's metadata addr is very high: %p (obj offset = 0x%lx, index = %u)\n", retval, offset, index);
		//}
		return retval;
		//return &shadow_map[index];
}


// Returns the metadata for the object that contains the given address
inline uint32_t ShadowMemory::getObjectMetadata(void * address) {
		if(!address) {
				return SHADOW_MEM_ERROR;
		}
		uint32_t * metadata = getObjectMetadataAddr(address);
		return *metadata;
}

// Returns the size of the object that contains the given address
size_t ShadowMemory::getObjectSize(void * address) {
		uint32_t metadata = getObjectMetadata(address);
		if(metadata == SHADOW_MEM_ERROR) {
				fprintf(stderr, "ERROR: could not find shadow memory metadata for "
								"object located at %p\n", address);
				abort();
		}
		return (size_t)(metadata >> SHADOW_MEM_CACHE_UTIL_BITS);
}

// Returns the cache line utilization for the given cache line within the
// object that contains the specified address
int ShadowMemory::getCacheLineUsage(void * address) {
		uint32_t metadata = getObjectMetadata(address);
		if(metadata == SHADOW_MEM_ERROR) {
				fprintf(stderr, "ERROR: could not find shadow memory metadata for "
								"object located at %p\n", address);
				abort();
		}
		return (size_t)(metadata & SHADOW_MEM_CACHE_UTIL_MASK);
}

/*
inline int ShadowMemory::key(size_t size) {
    int remainder = size % CACHEBLOCK;
    int waste = (remainder != 0) ? CACHEBLOCK - remainder : 0;
    return (size << CACHE_USED_BITS) | (0xFF & waste);
}
*/

// Updates information about a particular object in this shadow memory region
void ShadowMemory::updateObject(void * address, size_t size) {
		unsigned i;
    uint32_t * shadow_obj = getObjectMetadataAddr(address);
		size_t size_remaining = size;

		if(!shadow_obj) {
				fprintf(stderr, "ERROR: could not obtain shadow memory address for"
												"object associated with address %p (size=%zu)\n",
												address, size);
				abort();
		}
		
		for(i = 0; i < num_cache_lines(size); i++) {
				unsigned short cacheLineUsedBytes;

				// Determine how many bytes of the current cache line are used
				if(size_remaining >= CACHE_LINE_SIZE_BITS) {
						cacheLineUsedBytes = CACHE_LINE_SIZE_BITS;
				} else if(size_remaining <= 0) {
						cacheLineUsedBytes = 0;
				} else {
						cacheLineUsedBytes = size_remaining;
				}

				// Decrement the size_remaining counter --
				// it is possible for size_remaining to hold a negative value, but it will
				// never be less than -63 (this is to say that it is bounded on the
				// negative side)
				if(size_remaining > 0) {
						size_remaining -= CACHE_LINE_SIZE_BITS;
				}

				// Construct the current cache line's metadata value and
				// store it in the shadow memory
				uint32_t metadata = ((((int)size) << SHADOW_MEM_CACHE_UTIL_BITS) | cacheLineUsedBytes);
				shadow_obj[i] = metadata;
		}
}

int ShadowMemory::compare(ShadowMemory * other) {
		assert(addr != NULL);

		uintptr_t my_begin = (uintptr_t)getMappingBegin();
		uintptr_t other_begin = (uintptr_t)other->getMappingBegin();

		//fprintf(stderr, "%s %d : my_begin = 0x%lx, other_begin = 0x%lx\n", __FUNCTION__, __LINE__, my_begin, other_begin);
		if(my_begin == other_begin) {
				return 0;
		} else if(my_begin < other_begin) {
				return -1;
		} else {
				return 1;
		}
}

bool ShadowMemory::contains(void * address) {
	return ((uintptr_t)map_begin <= (uintptr_t)address && (uintptr_t)map_end > (uintptr_t)address);
}
