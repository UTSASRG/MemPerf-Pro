#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include "shadowmemory.hh"
#include "real.hh"

PageMapEntry ** ShadowMemory::mega_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_end = nullptr;
PageMapEntry * ShadowMemory::page_map_bump_ptr = nullptr;
CacheMapEntry * ShadowMemory::cache_map_begin = nullptr;
CacheMapEntry * ShadowMemory::cache_map_end = nullptr;
CacheMapEntry * ShadowMemory::cache_map_bump_ptr = nullptr;
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
			abort();			// temporary, remove and replace with return false after testing
	}

	// Allocate 4KB-to-cacheline region
	if((void *)(page_map_begin = (PageMapEntry *)mmap((void *)PAGE_MAP_START, PAGE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			perror("mmap of global page map failed");
			abort();			// temporary, remove and replace with return false after testing
	}
	page_map_end = page_map_begin + MAX_PAGE_MAP_ENTRIES;
	page_map_bump_ptr = page_map_begin;

	// Allocate cacheline map region
	if((void *)(cache_map_begin = (CacheMapEntry *)mmap((void *)CACHE_MAP_START, CACHE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			perror("mmap of cache map region failed");
			abort();			// temporary, remove and replace with return false after testing
	}
	cache_map_end = cache_map_begin + MAX_CACHE_MAP_ENTRIES;
	cache_map_bump_ptr = cache_map_begin;

	isInitialized = E_MAP_INIT_DONE; 

	//fprintf(stderr, "mega_map_begin = %p, page_map_begin = %p, cache_map_begin = %p\n",
	//		mega_map_begin, page_map_begin, cache_map_begin);

	return true;
}

unsigned ShadowMemory::updateObject(void * address, size_t size, bool isFree) {
		if(address == NULL) {
				fprintf(stderr, "ERROR: passed a NULL pointer into %s in %s:%d\n",
								__FUNCTION__, __FILE__, __LINE__);
				abort();
		}

		uintptr_t uintaddr = (uintptr_t)address;
		
		// First compute the megabyte number of the given address.
		unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
		if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
				fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
								uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
				abort();
		}

		unsigned firstPageIdx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);

		// This call to getMegaMapEntry is needed, even though we only use the return value for the
		// print statement that immediately follows it; this is because it initializes the entry in
		// the megabyte map using the page map bump pointer if the entry is null. In other words,
		// do not comment out or remove this line.
		//PageMapEntry ** mega_entry = getMegaMapEntry(mega_index);
		//fprintf(stderr, "> mega_entry = %p, *mega_entry = %p, mega_index = %lu\n", mega_entry, *mega_entry, mega_index);

		unsigned numNewPagesTouched = updatePages(uintaddr, mega_index, firstPageIdx, size, isFree);
		PageMapEntry::updateCacheLines(uintaddr, mega_index, firstPageIdx, size, isFree);
		if(!isFree) {
				updateObjectSize(uintaddr, size);
		}

		return numNewPagesTouched;
}

unsigned ShadowMemory::updatePages(uintptr_t uintaddr, unsigned long mega_index, unsigned page_index, unsigned size, bool isFree) {
		size_t classSize = 0;

		if(isFree) {
				// If this is a free operation, we will need the object's true size in order to calculate
				// the proper number of pages and cachelines needed to be updated.
				size = getObjectSize(uintaddr, mega_index, page_index);
		} else {
				// If this is an allocation request, we will also need the object's class size so that we may
				// store it in each PageMapEntry associated with each page of this object.
				if(bibop) {
						classSize = getClassSizeFor(size);
				} else {
						//classSize = libc_malloc_usable_size(size);
						classSize = malloc_usable_size((void *)uintaddr);
				}
		}

		//fprintf(stderr, "updatePages(%#lx, %u) : isFree ?= %s, bibop ?= %s, classSize = %zu, size = %u\n",
		//				uintaddr, size, boolToStr(isFree), boolToStr(bibop), classSize, size);

		unsigned curPageIdx;
		unsigned firstPageOffset = (uintaddr & PAGESIZE_MASK);
		//unsigned numPages = size >> LOG2_PAGESIZE;		// The actual number
																		// of *touched* pages could be up to
																		// two more than the value of numPages.
		unsigned curPageBytes;
		unsigned numNewPagesTouched = 0;
		int size_remain = size;
		PageMapEntry * current;

		//fprintf(stderr, ">   obj 0x%lx sz %u : numPages >= %u, page_index = %d, firstPageOffset = %u\n",
		//				uintaddr, size, numPages, page_index, firstPageOffset);

		curPageIdx = page_index;
		// First test to determine whether this object begins on a page boundary. If not, then we must
		// increment the used bytes field of the first page separately.
		if(firstPageOffset > 0) {
				// Fetch the page map entry for the page located in the specified megabyte.
				current = getPageMapEntry(mega_index, curPageIdx);
				curPageBytes = PAGESIZE - firstPageOffset;
				//fprintf(stderr, ">   obj 0x%lx sz %u : current = %p, updating page %u, contrib size = %u\n",
				//				uintaddr, size, current, curPageIdx, curPageBytes);
				if(isFree) {
						current->subUsedBytes(curPageBytes);
				} else {
						current->addUsedBytes(curPageBytes);
						if(!current->isTouched()) {
								current->setTouched();
								numNewPagesTouched++;
						}
				}
				size_remain -= curPageBytes;
				// Do not clear the page's class size on free -- this will be cleared
				// when the pages are re-utilized for use by a new object or mapping.
				if(!isFree) {
						current->setClassSize(classSize);
				}
				curPageIdx++;
		}
		// Next, loop until we have accounted for all object bytes...
		while(size_remain > 0) {
				current = getPageMapEntry(mega_index, curPageIdx);
				// If we still have at least a page of the object's size left to process, we can start
				// setting the target pages' used bytes to PAGESIZE, as nothing else should be located
				// there.
				if(size_remain >= PAGESIZE) {
						curPageBytes = PAGESIZE;
						//fprintf(stderr, ">   obj 0x%lx sz %u : current = %p, updating page %u, contrib size = %u\n",
						//				uintaddr, size, current, curPageIdx, curPageBytes);
						if(isFree) {
								current->setUsedBytes(0);
						} else {
								current->setUsedBytes(curPageBytes);
								if(!current->isTouched()) {
										current->setTouched();
										numNewPagesTouched++;
								}
						}
				// Otherwise, if less than a page of the object's size remains to be accounted for,
				// increment this final page by the amount remaining.
				} else {
						curPageBytes = size_remain;
						//fprintf(stderr, ">   obj 0x%lx sz %u : current = %p, updating page %u, contrib size = %u\n",
						//				uintaddr, size, current, curPageIdx, curPageBytes);
						if(isFree) {
								current->subUsedBytes(curPageBytes);
						} else {
								current->addUsedBytes(curPageBytes);
								if(!current->isTouched()) {
										current->setTouched();
										numNewPagesTouched++;
								}
						}
				}
				size_remain -= curPageBytes;
				// Do not clear the page's class size on free -- this will be cleared
				// when the pages are re-utilized for use by a new object or mapping.
				if(!isFree) {
						current->setClassSize(classSize);
				}
				curPageIdx++;
		}

		return numNewPagesTouched;
}

unsigned ShadowMemory::getPageClassSize(void * address) {
		uintptr_t uintaddr = (uintptr_t)address;

		unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
		if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
				fprintf(stderr, "ERROR: mega index of 0x%lx is too large: %lu > %u\n",
								uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
				abort();
		}

		unsigned page_index = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);

		return getPageClassSize(mega_index, page_index);
}

unsigned ShadowMemory::getPageClassSize(unsigned long mega_index, unsigned page_index) {
		PageMapEntry * current = getPageMapEntry(mega_index, page_index);
		return current->getClassSize();
}

unsigned ShadowMemory::getObjectSize(uintptr_t uintaddr, unsigned long mega_index, unsigned page_index) {
		unsigned classSize = getPageClassSize(mega_index, page_index);

		// Check to see if this is a large object -- we will have no size class info if it is.
		//if(getClassSizeFor(classSize) > malloc_mmap_threshold) {
		if(classSize > malloc_mmap_threshold) {
				return classSize;	
		}
		
		uint32_t * ptrToSentinel = (uint32_t *)(uintaddr + classSize - OBJECT_SIZE_SENTINEL_SIZE);
		//fprintf(stderr, "getObjectSize(%#lx, %lu, %u) : bibop ?= %s, page's classSize = %u, sentinel @ %p\n",
		//				uintaddr, mega_index, page_index, boolToStr(bibop), classSize, ptrToSentinel);

		//uint32_t * ptrToSentinel = (uint32_t *)(uintaddr + classSize - OBJECT_SIZE_SENTINEL_SIZE);
		//fprintf(stderr, "getObjectSize(%#lx, %lu, %u) : bibop ?= %s, page's classSize = %u, sentinel @ %p, sentinel bytes = 0x%x\n",
		//				uintaddr, mega_index, page_index, boolToStr(bibop), classSize, ptrToSentinel, *ptrToSentinel);

		if(((*ptrToSentinel) & OBJECT_SIZE_SENTINEL_MASK) == OBJECT_SIZE_SENTINEL) {
				uint32_t objSize = (*ptrToSentinel) & OBJECT_SIZE_MASK;
				//fprintf(stderr, "> sentinel found @ %p, object size = %u\n",
				//				ptrToSentinel, objSize);
				return objSize;
		} else {
				if(isLibc) {
						classSize = malloc_usable_size((void *)uintaddr);
				}
				//fprintf(stderr, "> sentinel not found, using class size of %u\n", classSize);
				return classSize;
		}
}

unsigned ShadowMemory::getObjectSize(void * address) {
		uintptr_t uintaddr = (uintptr_t)address;

		// First compute the megabyte number of the given address.
		unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
		if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
				fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
								uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
				abort();
		}

		unsigned page_index = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);

		return getObjectSize(uintaddr, mega_index, page_index);
}


bool ShadowMemory::updateObjectSize(uintptr_t uintaddr, unsigned size) {
		unsigned classSize;
		if(bibop) {
				classSize = getClassSizeFor(size);
		} else {
				classSize = libc_malloc_usable_size(size);
		}

		if(classSize > malloc_mmap_threshold) {
				classSize = getPageClassSize((void *)uintaddr);
		}

		// We cannot (nor should we need to) write a size of zero in the last bytes of a freed object;
		// this is critical because libc uses this area to store its own object sizes after the object
		// has been freed.
		if((size > 0) && (size <= MAX_OBJECT_SIZE) && (size <= malloc_mmap_threshold) && (classSize - size >= OBJECT_SIZE_SENTINEL_SIZE)) {
				uint32_t * ptrToSentinel = (uint32_t *)(uintaddr + classSize - OBJECT_SIZE_SENTINEL_SIZE);
				/*
				fprintf(stderr, "updateObjectSize(%#lx, %u) : bibop ?= %s, classSize = %u, sentinel @ %p, sentinel bytes = 0x%x\n",
								uintaddr, size, boolToStr(bibop), classSize, ptrToSentinel, *ptrToSentinel);

				if(((*ptrToSentinel) & OBJECT_SIZE_SENTINEL_MASK) == OBJECT_SIZE_SENTINEL) {
						uint32_t objSize = (*ptrToSentinel) & OBJECT_SIZE_MASK;
						fprintf(stderr, "> sentinel found @ %p, current object size is %u, updating to %u\n",
										ptrToSentinel, objSize, size);
				} else {
						fprintf(stderr, "> previous sentinel not found, updating object size to %u\n", size);
				}
				*/
				*ptrToSentinel = OBJECT_SIZE_SENTINEL | size;
		} else {
				//fprintf(stderr, "> object size is too close to class size to have a sentinel area -- will use page's classSize value later\n");
		}

		return true;
}

inline unsigned ShadowMemory::libc_malloc_usable_size(unsigned size) {
		if(size <= LIBC_MIN_OBJECT_SIZE) {
				return LIBC_MIN_OBJECT_SIZE;
		} else if(size >= malloc_mmap_threshold) {
				return alignup(size, PAGESIZE);
		}
		unsigned next_mult_16 = ((~size & 0xf) + size + 1);
		if(next_mult_16 - size >= LIBC_METADATA_SIZE) {
				return (next_mult_16 - LIBC_METADATA_SIZE);
		} else {
				return (next_mult_16 + LIBC_METADATA_SIZE);
		}
}

unsigned ShadowMemory::cleanupPages(uintptr_t uintaddr, size_t length) {
		unsigned numTouchedPages = 0;

		// First compute the megabyte number of the given address.
		unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
		if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
				fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
								uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
				abort();
		}

		unsigned firstPageIdx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);

		//PageMapEntry ** mega_entry = getMegaMapEntry(mega_index);
		//fprintf(stderr, "> mega_entry = %p, *mega_entry = %p, mega_index = %lu\n", mega_entry, *mega_entry, mega_index);

		length = alignup(length, PAGESIZE);
		//fprintf(stderr, "%s(%#lx, %zu)\n", __FUNCTION__, uintaddr, length);


		unsigned curPageIdx;
		unsigned numPages = length >> LOG2_PAGESIZE;
		PageMapEntry * current;

		//fprintf(stderr, "> obj %#lx len %zu : numPages = %u, firstPageIdx = %u\n",
		//				uintaddr, length, numPages, firstPageIdx);

		// Next, loop until we have accounted for all mapping bytes...
		for(curPageIdx = firstPageIdx; curPageIdx < firstPageIdx + numPages; curPageIdx++) {
				current = getPageMapEntry(mega_index, curPageIdx);
				//fprintf(stderr, "> obj %#lx len %zu : current = %p, updating page %u to size 0\n",
				//				uintaddr, length, current, curPageIdx);
				if(current->isTouched()) {
						numTouchedPages++;
				}
				current->clear();
		}

		return numTouchedPages;
}

void PageMapEntry::clear() {
		//cache_map_entry = NULL;
		if(cache_map_entry) {
				size_t cache_entries_size = NUM_CACHELINES_PER_PAGE * sizeof(CacheMapEntry);
				memset(cache_map_entry, 0, cache_entries_size);
		}
		touched = false;
		num_used_bytes = 0;
		classSize = 0;
}

unsigned PageMapEntry::getClassSize() {
		return classSize;
}

void PageMapEntry::setClassSize(unsigned size) {
		classSize = size;
}

bool PageMapEntry::isTouched() {
		return touched;
}

void PageMapEntry::setTouched() {
		touched = true;
}

void PageMapEntry::clearTouched() {
		touched = false;
}

PageMapEntry * ShadowMemory::getPageMapEntry(unsigned long mega_idx, unsigned page_idx) {
		unsigned rel_mega_index = mega_idx;
		unsigned rel_page_index = page_idx;

		if(page_idx > NUM_PAGES_PER_MEGABYTE) {
				rel_mega_index += (page_idx >> LOG2_NUM_PAGES_PER_MEGABYTE);
				rel_page_index = (page_idx & NUM_PAGES_PER_MEGABYTE_MASK);
		}

		PageMapEntry ** mega_entry = getMegaMapEntry(rel_mega_index);
		PageMapEntry * targetPage = (*mega_entry + rel_page_index);

		//fprintf(stderr, "> getPageMapEntry(%lu, %u) -> rel_mega_index=%u, rel_page_index=%u, targetPage=%p\n",
					//mega_idx, page_idx, rel_mega_index, rel_page_index, targetPage);

		return targetPage;
		//return (*mega_entry + rel_page_index);
}

PageMapEntry ** ShadowMemory::getMegaMapEntry(unsigned long mega_index) {
		PageMapEntry ** mega_entry = mega_map_begin + mega_index;

		if(__builtin_expect(*mega_entry == NULL, 0)) {
				// Create a new page entries
				*mega_entry = doPageMapBumpPointer();
				//fprintf(stderr, "> entry not found: page map bump ptr is now = %p\n", page_map_bump_ptr);
		} else {
				// Update the existing entries
				//fprintf(stderr, "> found! mega_entry = %p, *mega_entry = %p\n", mega_entry, *mega_entry);
		}

		return mega_entry;
}

CacheMapEntry * ShadowMemory::doCacheMapBumpPointer() {
		cache_map_bump_ptr += NUM_CACHELINES_PER_PAGE;
		if(cache_map_bump_ptr >= cache_map_end) {
				fprintf(stderr, "ERROR: cache map out of memory\n");
				abort();
		}
		return cache_map_bump_ptr;
}

PageMapEntry * ShadowMemory::doPageMapBumpPointer() {
		page_map_bump_ptr += NUM_PAGES_PER_MEGABYTE;
		if(page_map_bump_ptr >= page_map_end) {
				fprintf(stderr, "ERROR: page map out of memory\n");
				abort();
		}
		return page_map_bump_ptr;
}



// Accepts absolute values only (i.e., it is required that 0 <= page_idx < 256, 0 <= cache_idx < 64).
// For use of relative index values, see getCacheMapEntry(cache_idx).
CacheMapEntry * PageMapEntry::getCacheMapEntry(unsigned long mega_idx, unsigned page_idx, unsigned cache_idx) {
		unsigned target_cache_idx = cache_idx & CACHELINES_PER_PAGE_MASK;
		unsigned rel_page_idx = cache_idx >> LOG2_NUM_CACHELINES_PER_PAGE;
		unsigned target_page_idx = page_idx + rel_page_idx;
		unsigned target_mega_idx = target_page_idx >> LOG2_NUM_PAGES_PER_MEGABYTE;

		PageMapEntry * targetPage = ShadowMemory::getPageMapEntry(target_mega_idx, target_page_idx);
		CacheMapEntry * targetCache = targetPage->getCacheMapEntry_helper() + target_cache_idx;

		//fprintf(stderr, "> getCacheMapEntry(%lu, %u, %u) -> rel_page_idx=%u, target_mega_idx=%u, target_page_idx=%u, target_cache_idx=%u\n"
		//								"-> targetPage=%p, targetCache=%p\n", mega_idx, page_idx, cache_idx, rel_page_idx, target_mega_idx, target_page_idx,
		//								target_cache_idx, targetPage, targetCache);

		return targetCache;
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
		if(num_used_bytes < 0) {
				//fprintf(stderr, "subUsedBytes(%u) : PageMapEntry=%p : WARNING: num_used_bytes == %u < 0\n",
				//				num_bytes, this, num_used_bytes);
				num_used_bytes = 0;
		} else if(num_used_bytes > PAGESIZE) {
				//fprintf(stderr, "subUsedBytes(%u) : PageMapEntry=%p : WARNING: num_used_bytes == %u > %d\n",
				//				num_bytes, this, num_used_bytes, PAGESIZE);
				num_used_bytes = PAGESIZE;
		}
		return true;
}

bool PageMapEntry::updateCacheLines(uintptr_t uintaddr, unsigned long mega_index, unsigned page_index, unsigned size, bool isFree) {
		if(isFree) {
				size = ShadowMemory::getObjectSize(uintaddr, mega_index, page_index);
		}

		unsigned firstCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
		unsigned firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
		//unsigned numCacheLines = size >> LOG2_CACHELINE_SIZE;				// The actual number of
																						// *touched* cache lines could be up to two
																						// more than the value of numCacheLines.
		unsigned curCacheLineIdx;
		unsigned curCacheLineBytes;
		int size_remain = size;
		CacheMapEntry * current;

		//fprintf(stderr, "> obj 0x%lx sz %u : numCacheLines >= %u, firstCacheLineIdx = %d, firstCacheLineOffset = %u\n",
		//				uintaddr, size, numCacheLines, firstCacheLineIdx, firstCacheLineOffset);

		curCacheLineIdx = firstCacheLineIdx;
		if(firstCacheLineOffset) {
				current = getCacheMapEntry(mega_index, page_index, curCacheLineIdx);
				curCacheLineBytes = CACHELINE_SIZE - firstCacheLineOffset;
				//fprintf(stderr, "> obj 0x%lx sz %u : current = %p, updating cache line %u, contrib size = %u\n",
				//				uintaddr, size, current, curCacheLineIdx, curCacheLineBytes);
				if(isFree) {
						current->subUsedBytes(curCacheLineBytes);
				} else {
						current->addUsedBytes(curCacheLineBytes);
				}
				size_remain -= curCacheLineBytes;
				current->setOwner(thrData.tid);
				curCacheLineIdx++;
		}
		while(size_remain > 0) {
				current = getCacheMapEntry(mega_index, page_index, curCacheLineIdx);
				if(size_remain >= CACHELINE_SIZE) {
						curCacheLineBytes = CACHELINE_SIZE;
						//fprintf(stderr, "> obj 0x%lx sz %u : current = %p, updating cache line %u, contrib size = %u\n",
						//				uintaddr, size, current, curCacheLineIdx, curCacheLineBytes);
						if(isFree) {
								current->setUsedBytes(0);
						} else {
								current->setUsedBytes(curCacheLineBytes);
						}
				} else {
						curCacheLineBytes = size_remain;
						//fprintf(stderr, "> obj 0x%lx sz %u : current = %p, updating cache line %u, contrib size = %u\n",
						//				uintaddr, size, current, curCacheLineIdx, curCacheLineBytes);
						if(isFree) {
								current->subUsedBytes(curCacheLineBytes);
						} else {
								current->addUsedBytes(curCacheLineBytes);
						}
				}
				size_remain -= curCacheLineBytes;
				current->setOwner(thrData.tid);
				curCacheLineIdx++;
		}

		return true;
}

pid_t CacheMapEntry::getOwner() {
		return owner;
}

void CacheMapEntry::setOwner(pid_t new_owner) {
		owner = new_owner;
}

CacheMapEntry * PageMapEntry::getCacheMapEntry_helper() {
		if(__builtin_expect(cache_map_entry == NULL, 0)) {
				// Create a new entries
				//ShadowMemory::cache_map_bump_ptr += NUM_CACHELINES_PER_PAGE;
				//cache_map_entry = ShadowMemory::cache_map_bump_ptr;
				cache_map_entry = ShadowMemory::doCacheMapBumpPointer();
				//fprintf(stderr, "> cur page cache map entry is empty: cache map bump ptr is now = %p\n", cache_map_entry);
		} else {
				// Update the existing entries
				//fprintf(stderr, "> cur page cache map entry is found! cache_map_entry = %p\n", cache_map_entry);
		}

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

const char * boolToStr(bool p) {
		return (p ? "true" : "false");
}
