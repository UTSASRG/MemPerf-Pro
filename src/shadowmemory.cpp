#include <stdio.h>
#include <malloc.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include "shadowmemory.hh"
#include "real.hh"

PageMapEntry ** ShadowMemory::mega_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_begin = nullptr;
uintptr_t * ShadowMemory::obj_size_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_bump_ptr = nullptr;
uintptr_t * ShadowMemory::obj_size_map_bump_ptr = nullptr;
uintptr_t * ShadowMemory::cache_map_begin = nullptr;
uintptr_t * ShadowMemory::cache_map_bump_ptr = nullptr;
eMapInitStatus ShadowMemory::isInitialized = E_MAP_INIT_NOT;

bool ShadowMemory::initialize() {
	if(isInitialized != E_MAP_INIT_NOT) {
			return false;
	}
	isInitialized = E_MAP_INIT_WORKING; 

	// Allocate 1MB-to-4KB mapping region
	if((void *)(mega_map_begin = (PageMapEntry **)mmap((void *)MEGABYTE_MAP_START, MEGABYTE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			perror("mmap of global megabyte map failed");
			//return false;
			abort();			// temporary, remove and replace with return false after testing
	}

	// Allocate 4KB-to-cacheline region
	if((void *)(page_map_begin = (PageMapEntry *)mmap((void *)PAGE_MAP_START, PAGE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			perror("mmap of global page map failed");
			//return false;
			abort();			// temporary, remove and replace with return false after testing
	}
	page_map_bump_ptr = page_map_begin;

	// Allocate cacheline map region
	if((void *)(cache_map_begin = (uintptr_t *)mmap((void *)CACHE_MAP_START, CACHE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			perror("mmap of cache map region failed");
			//return false;
			abort();			// temporary, remove and replace with return false after testing
	}
	cache_map_bump_ptr = cache_map_begin;

	// Allocate word size-to-object-size region
	if((void *)(obj_size_map_begin = (uintptr_t *)mmap((void *)OBJ_SIZE_MAP_START, OBJ_SIZE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			perror("mmap of object size region failed");
			//return false;
			abort();			// temporary, remove and replace with return false after testing
	}
	obj_size_map_bump_ptr = obj_size_map_begin;

	isInitialized = E_MAP_INIT_DONE; 

	fprintf(stderr, "mega_map_begin = %p, page_map_begin = %p, cache_map_begin = %p, obj_size_map_begin = %p\n",
			mega_map_begin, page_map_begin, cache_map_begin, obj_size_map_begin);

	return true;
}

void ShadowMemory::updateObject(void * address, size_t size) {
		if(!address) {
				fprintf(stderr, "ERROR: passed a NULL pointer into %s in %s:%d\n",
								__FUNCTION__, __FILE__, __LINE__);
				abort();
		}

		uintptr_t uintaddr = (uintptr_t)address;

		// First compute the megabyte number of the given address.
		unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
		if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
				fprintf(stderr, "ERROR: mega index of %p too large: %lu > %u\n",
								address, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
				abort();
		}

		PageMapEntry ** mega_entry = mega_map_begin + mega_index;

		if(*mega_entry == NULL) {
				// Create a new entries
				page_map_bump_ptr += NUM_PAGE_MAP_ENTRIES_PER_MB;
				*mega_entry = page_map_bump_ptr;
				fprintf(stderr, "> entry not found: page map bump ptr is now = %p\n", page_map_bump_ptr);
		} else {
				// Update the existing entries
				fprintf(stderr, "> found! mega_entry = %p, *mega_entry = %p\n", mega_entry, *mega_entry);
		}

		// Find the correct PageMapEntry for this address
		unsigned page_index = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
		assert(page_index < NUM_PAGE_MAP_ENTRIES_PER_MB);
		PageMapEntry * current = *mega_entry + page_index;

		fprintf(stderr, "> mega_entry = %p, *mega_entry = %p, mega_index = %lu, page_index = %u\n", mega_entry, *mega_entry, mega_index, page_index);
		if(size > PAGESIZE) {
				// This object spans numObjPages whole pages, and possibly one additional remainder page
				unsigned numObjPages = size >> LOG2_PAGESIZE;
				unsigned objSizePageRem = size & PAGESIZE_MASK;
				unsigned i;

				for(i = 0; i < numObjPages; i++, current++) {
						current->setUsedBytes(PAGESIZE);
						current->setPageMonoObject(true);
				}
				current->addUsedBytes(objSizePageRem);
				current->setPageMonoObject(false);

				fprintf(stderr, "> DEBUG: object spans %u whole pages, and adds %u to a remainder page\n", numObjPages, objSizePageRem);
		} else {
				current->addUsedBytes(size);
		}

		// fetch the cache line object now and loop through it
		current->updateCacheLines(uintaddr, size);

		/*
			 PageMapEntry * entry = *mega_entry;
			 for(i = 0; i < NUM_PAGE_MAP_ENTRIES_PER_MB; i++, entry++) {
			 entry->
			 }
		 */
}



void PageMapEntry::initialize() { }

CacheMapEntry * PageMapEntry::getCacheMapEntry(unsigned cache_idx) {
		unsigned rel_page_index = cache_idx >> LOG2_NUM_CACHELINES_PER_PAGE;
		PageMapEntry * targetPage = this + rel_page_index;

		fprintf(stderr, "> getCacheMapEntry(%u) -> rel_page_index=%u, targetPage=%p\n",
					cache_idx, rel_page_index, targetPage);
		return targetPage->getCacheMapEntry_helper();
}

unsigned PageMapEntry::getSizeMapIndex() {
		return obj_size_index;
}

bool PageMapEntry::setUsedBytes(unsigned num_bytes) {
		assert(num_bytes <= PAGESIZE);
		num_used_bytes = num_bytes;
		return true;
}

bool PageMapEntry::addUsedBytes(unsigned num_bytes) {
		assert(num_bytes <= PAGESIZE);
		assert(num_used_bytes + num_bytes <= PAGESIZE);
		num_used_bytes += num_bytes;
		return true;
}

bool PageMapEntry::subUsedBytes(unsigned num_bytes) {
		assert(num_bytes <= PAGESIZE);
		assert(num_used_bytes - num_bytes >= 0);
		num_used_bytes -= num_bytes;
		return true;
}

bool PageMapEntry::isPageMonoObject() {
		return isPageMonoObj;
}

void PageMapEntry::setPageMonoObject(bool status) {
		isPageMonoObj = status;
}

bool PageMapEntry::updateCacheLines(uintptr_t uintaddr, unsigned size) {
		unsigned firstCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
		//unsigned numCacheLines = alignup(size, CACHELINE_SIZE) >> LOG2_CACHELINE_SIZE;
		unsigned numCacheLines = size >> LOG2_CACHELINE_SIZE;
		unsigned objSizeCacheRem = size & CACHELINE_SIZE_MASK;
		unsigned i;
		CacheMapEntry * current;

		fprintf(stderr, "> obj 0x%lx sz %u : updating %u cachelines starting at current page's cacheline %d...\n",
						uintaddr, size, numCacheLines, firstCacheLineIdx);
		for(i = firstCacheLineIdx; i < firstCacheLineIdx + numCacheLines; i++) {
				current = getCacheMapEntry(i);
				current->setUsedBytes(CACHELINE_SIZE);
		}
		if(objSizeCacheRem) {
				fprintf(stderr, "> obj 0x%lx sz %u : updating final remainder (%u) cacheline at CL offset %u within current page...\n",
								uintaddr, size, objSizeCacheRem, firstCacheLineIdx + numCacheLines);
				current = getCacheMapEntry(firstCacheLineIdx + numCacheLines);
				current->addUsedBytes(objSizeCacheRem);
		}

		return true;
}

CacheMapEntry * PageMapEntry::getCacheMapEntry_helper() {
		return cache_map_entry;
}


unsigned char CacheMapEntry::setUsedBytes(unsigned num_bytes) {
		assert(num_bytes <= PAGESIZE);
		num_used_bytes = num_bytes;
		return true;
}

unsigned char CacheMapEntry::addUsedBytes(unsigned num_bytes) {
		assert(num_bytes <= PAGESIZE);
		assert(num_used_bytes + num_bytes <= PAGESIZE);
		num_used_bytes += num_bytes;
		return true;
}

unsigned char CacheMapEntry::subUsedBytes(unsigned num_bytes) {
		assert(num_bytes <= PAGESIZE);
		assert(num_used_bytes - num_bytes >= 0);
		num_used_bytes -= num_bytes;
		return true;
}
