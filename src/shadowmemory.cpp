#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include "shadowmemory.hh"
#include "real.hh"
#include "memwaste.h"
#include "spinlock.hh"
#include "threadlocalstatus.h"

PageMapEntry ** ShadowMemory::mega_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_end = nullptr;
PageMapEntry * ShadowMemory::page_map_bump_ptr = nullptr;
CacheMapEntry * ShadowMemory::cache_map_begin = nullptr;
CacheMapEntry * ShadowMemory::cache_map_end = nullptr;
CacheMapEntry * ShadowMemory::cache_map_bump_ptr = nullptr;
spinlock ShadowMemory::cache_map_lock;
spinlock ShadowMemory::mega_map_lock;
bool ShadowMemory::isInitialized;

bool ShadowMemory::initialize() {
	if(isInitialized) {
			return false;
	}
    cache_map_lock.init();
    mega_map_lock.init();

	// Allocate 1MB-to-4KB mapping region
	if((void *)(mega_map_begin = (PageMapEntry **)mmap((void *)MEGABYTE_MAP_START, MEGABYTE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			fprintf(stderr, "mmap of global megabyte map failed. Adddress %lx error %s\n", MEGABYTE_MAP_START, strerror(errno));
	}

	// Allocate 4KB-to-cacheline region
	if((void *)(page_map_begin = (PageMapEntry *)mmap((void *)PAGE_MAP_START, PAGE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
    fprintf(stderr, "errno %d\n", ENOMEM);
			fprintf(stderr, "mmap of global page map failed. Adddress %lx size %lx error (%d) %s\n", PAGE_MAP_START, PAGE_MAP_SIZE, errno, strerror(errno));
      while(1) { ;} 
//			abort();			// temporary, remove and replace with return false after testing
	}
	page_map_end = page_map_begin + MAX_PAGE_MAP_ENTRIES;
	page_map_bump_ptr = page_map_begin;

	// Allocate cacheline map region
	if((void *)(cache_map_begin = (CacheMapEntry *)mmap((void *)CACHE_MAP_START, CACHE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			perror("mmap of cache map region failed");
	}
	cache_map_end = cache_map_begin + MAX_CACHE_MAP_ENTRIES;
	cache_map_bump_ptr = cache_map_begin;

	isInitialized = true;

	return true;
}

size_t ShadowMemory::updateObject(void * address, size_t size, bool isFree) {
    if(address == nullptr || size == 0) {
        return 0;
    }

    //unsigned classSize;
    uintptr_t uintaddr = (uintptr_t)address;

    // First compute the megabyte number of the given address.
    unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
    if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
        fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
                uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
        abort();
    }

    unsigned firstPageIdx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
    unsigned numNewPagesTouched = updatePages(uintaddr, mega_index, firstPageIdx, size, isFree);
    PageMapEntry::updateCacheLines(uintaddr, mega_index, firstPageIdx, size, isFree);

    return numNewPagesTouched * PAGESIZE;
}

unsigned ShadowMemory::updatePages(uintptr_t uintaddr, unsigned long mega_index, unsigned page_index, unsigned size, bool isFree) {

		unsigned curPageIdx;
		unsigned firstPageOffset = (uintaddr & PAGESIZE_MASK);
		unsigned int curPageBytes;
		unsigned numNewPagesTouched = 0;
		int size_remain = size;
		PageMapEntry * current;

		curPageIdx = page_index;
		// First test to determine whether this object begins on a page boundary. If not, then we must
		// increment the used bytes field of the first page separately.

		if(firstPageOffset > 0) {
				// Fetch the page map entry for the page located in the specified megabyte.
				current = getPageMapEntry(mega_index, curPageIdx);
				if(firstPageOffset + size_remain >= PAGESIZE) {
						curPageBytes = PAGESIZE - firstPageOffset;
				} else {
						curPageBytes = size_remain;
				}
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
				} else {
						curPageBytes = size_remain;
				}

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
				curPageIdx++;
		}

		return numNewPagesTouched;
}

size_t ShadowMemory::cleanupPages(uintptr_t uintaddr, size_t length) {
		unsigned numTouchedPages = 0;

		// First compute the megabyte number of the given address.
		unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
		if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
				fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
								uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
				abort();
		}

		unsigned firstPageIdx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);

		length = alignup(length, PAGESIZE);


		unsigned curPageIdx;
		unsigned numPages = length >> LOG2_PAGESIZE;
		PageMapEntry * current;
		for(curPageIdx = firstPageIdx; curPageIdx < firstPageIdx + numPages; curPageIdx++) {
            current = getPageMapEntry(mega_index, curPageIdx);
            if(current->isTouched()) {
                numTouchedPages++;
                current->clear();
            }
        }
    return PAGESIZE * numTouchedPages;
}

void ShadowMemory::doMemoryAccess(uintptr_t uintaddr, eMemAccessType accessType) {

		PageMapEntry * pme;
		CacheMapEntry * cme;

		map_tuple tuple = ShadowMemory::getMapTupleByAddress(uintaddr);
		PageMapEntry ** mega_entry = mega_map_begin + tuple.mega_index;
		if(__atomic_load_n(mega_entry, __ATOMIC_RELAXED) == NULL) {
				return;
		}
		pme = (*mega_entry + tuple.page_index);

		if(!pme->isTouched()) {
				return;
		}

    if((cme = pme->getCacheMapEntry(false)) == NULL) {
            return;
        }

//    fprintf(stderr, "sample: addr = %p, type = %d\n", uintaddr, accessType);

    cme += tuple.cache_index;

    ThreadLocalStatus::friendlinessStatus.recordANewSampling(cme->getUsedBytes(), pme->getUsedBytes());
    if(accessType == E_MEM_STORE) {
        ThreadLocalStatus::friendlinessStatus.numOfSampledStoringInstructions++;
        if(cme->lastWriterThreadIndex != ThreadLocalStatus::runningThreadIndex && cme->lastWriterThreadIndex != -1) {
            ThreadLocalStatus::friendlinessStatus.numOfSampledFalseSharingInstructions[cme->falseSharingStatus]++;
            if(cme->falseSharingLineRecorded[cme->falseSharingStatus] == false) {
                ThreadLocalStatus::friendlinessStatus.numOfSampledFalseSharingCacheLines[cme->falseSharingStatus]++;
                cme->falseSharingLineRecorded[cme->falseSharingStatus] = true;
            }
        }
        cme->lastWriterThreadIndex = ThreadLocalStatus::runningThreadIndex;
    }
    if(cme->sampled == false) {
        ThreadLocalStatus::friendlinessStatus.numOfSampledCacheLines++;
        cme->sampled = true;
    }

}

map_tuple ShadowMemory::getMapTupleByAddress(uintptr_t uintaddr) {
		if(uintaddr == (uintptr_t)NULL) {
				fprintf(stderr, "ERROR: null pointer passed into %s at %s:%d\n",
								__FUNCTION__, __FILE__, __LINE__);
				abort();
		}

		// First compute the megabyte number of the given address.
		unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
		if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
            fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
								uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
            abort();
        }

    unsigned page_index =
            ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
    unsigned cache_index =
            ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);

    return {mega_index, page_index, cache_index};
}

void PageMapEntry::clear() {
		if(cache_map_entry) {
				size_t cache_entries_size = NUM_CACHELINES_PER_PAGE * sizeof(CacheMapEntry);
				memset(cache_map_entry, 0, cache_entries_size);
		}
		touched = false;
		num_used_bytes = 0;
}

unsigned int CacheMapEntry::getUsedBytes() {
    if(num_used_bytes > CACHELINE_SIZE) {
        return CACHELINE_SIZE;
    }
    if(num_used_bytes < 0) {
        return 0;
    }
    return num_used_bytes;

}

unsigned int PageMapEntry::getUsedBytes() {
    if(num_used_bytes > PAGESIZE) {
        return PAGESIZE;
    }
    if(num_used_bytes < 0) {
        return 0;
    }
    return num_used_bytes;
}

bool PageMapEntry::isTouched() {
		return touched;
}

void PageMapEntry::setTouched() {
		touched = true;
}

PageMapEntry * ShadowMemory::getPageMapEntry(unsigned long mega_idx, unsigned page_idx) {
		if(page_idx >= NUM_PAGES_PER_MEGABYTE) {
				mega_idx += (page_idx >> LOG2_NUM_PAGES_PER_MEGABYTE);
				page_idx &= NUM_PAGES_PER_MEGABYTE_MASK;
		}
		PageMapEntry ** mega_entry = getMegaMapEntry(mega_idx);

		return (*mega_entry + page_idx);
}

PageMapEntry ** ShadowMemory::getMegaMapEntry(unsigned long mega_index) {
		PageMapEntry ** mega_entry = mega_map_begin + mega_index;

		if(__builtin_expect(__atomic_load_n(mega_entry, __ATOMIC_RELAXED) == NULL, 0)) {

            mega_map_lock.lock();
				if(__builtin_expect(__atomic_load_n(mega_entry, __ATOMIC_RELAXED) == NULL, 1)) {
						__atomic_store_n(mega_entry, doPageMapBumpPointer(), __ATOMIC_RELAXED);
				}

            mega_map_lock.unlock();
		}

		return mega_entry;
}

CacheMapEntry * ShadowMemory::doCacheMapBumpPointer() {
		CacheMapEntry * curPtrValue = cache_map_bump_ptr;
		cache_map_bump_ptr += NUM_CACHELINES_PER_PAGE;
		if(cache_map_bump_ptr >= cache_map_end) {
				fprintf(stderr, "ERROR: cache map out of memory\n");
				abort();
		}
		return curPtrValue;
}

PageMapEntry * ShadowMemory::doPageMapBumpPointer() {
		PageMapEntry * curPtrValue = page_map_bump_ptr;
		page_map_bump_ptr += NUM_PAGES_PER_MEGABYTE;
		if(page_map_bump_ptr >= page_map_end) {
				fprintf(stderr, "ERROR: page map out of memory\n");
				abort();
		}
		return curPtrValue;
}

// Accepts relative page and cache indices -- for example, mega_idx=n, page_idx=257, cache_idx=65 would
// access mega_idx=n+1, page_idx=2, cache_idx=1.
CacheMapEntry * PageMapEntry::getCacheMapEntry(unsigned long mega_idx, unsigned page_idx, unsigned cache_idx) {
		unsigned target_cache_idx = cache_idx & CACHELINES_PER_PAGE_MASK;
		unsigned calc_overflow_pages = cache_idx >> LOG2_NUM_CACHELINES_PER_PAGE;
		unsigned rel_page_idx = page_idx + calc_overflow_pages;
		unsigned target_page_idx = rel_page_idx & NUM_PAGES_PER_MEGABYTE_MASK;
		unsigned calc_overflow_megabytes = rel_page_idx >> LOG2_NUM_PAGES_PER_MEGABYTE;
		unsigned target_mega_idx = mega_idx + calc_overflow_megabytes;

		PageMapEntry * targetPage = ShadowMemory::getPageMapEntry(target_mega_idx, target_page_idx);
		CacheMapEntry * baseCache = targetPage->getCacheMapEntry(true);

		return (baseCache + target_cache_idx);
}

bool PageMapEntry::addUsedBytes(unsigned int num_bytes) {
    num_used_bytes += num_bytes;
		return true;
}

bool PageMapEntry::subUsedBytes(unsigned int num_bytes) {
    num_used_bytes -= num_bytes;
		return true;
}

bool PageMapEntry::updateCacheLines(uintptr_t uintaddr, unsigned long mega_index, unsigned page_index, size_t size, bool isFree) {
		unsigned firstCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
		unsigned firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
		unsigned curCacheLineIdx;
		unsigned char curCacheLineBytes;
		int size_remain = size;
		CacheMapEntry * current;
//        fprintf(stderr, "tid = %llu, addr = %p, sz = %u, pidx = %lu, idx = %u, free = %d\n", ThreadLocalStatus::runningThreadIndex, uintaddr, size, page_index, firstCacheLineIdx, isFree);
		curCacheLineIdx = firstCacheLineIdx;
		if(firstCacheLineOffset) {
				current = getCacheMapEntry(mega_index, page_index, curCacheLineIdx);
				if(firstCacheLineOffset + size_remain >= CACHELINE_SIZE) {
						curCacheLineBytes = CACHELINE_SIZE - firstCacheLineOffset;
				} else {
						curCacheLineBytes = size_remain;
				}
				if(isFree) {
                    current->subUsedBytes(curCacheLineBytes);
                    if(current->getUsedBytes() == 0) {
                        current->lastWriterThreadIndex = -1;
                    }
                    if(current->falseSharingStatus != PASSIVE) {
                        if(current->lastFreeThreadIndex != ThreadLocalStatus::runningThreadIndex && current->lastFreeThreadIndex != -1) {
                            current->falseSharingStatus = PASSIVE;
                        }
                        current->lastAllocatingThreadIndex = -1;
                        current->lastFreeThreadIndex = ThreadLocalStatus::runningThreadIndex;
                    }
				} else {
                    current->addUsedBytes(curCacheLineBytes);
                    if(current->falseSharingStatus != PASSIVE) {
                        if(current->lastAllocatingThreadIndex != ThreadLocalStatus::runningThreadIndex && current->lastAllocatingThreadIndex != -1) {
                                current->falseSharingStatus = ACTIVE;
                        }
                        current->lastFreeThreadIndex = -1;
                        current->lastAllocatingThreadIndex = ThreadLocalStatus::runningThreadIndex;
				    }
				}

				size_remain -= curCacheLineBytes;
				curCacheLineIdx++;

		}
		while(size_remain > 0) {
            current = getCacheMapEntry(mega_index, page_index, curCacheLineIdx);

            if (size_remain >= CACHELINE_SIZE) {
                curCacheLineBytes = CACHELINE_SIZE;
            } else {
                curCacheLineBytes = size_remain;
            }

            if (isFree) {
                current->subUsedBytes(curCacheLineBytes);
            } else {
                current->addUsedBytes(curCacheLineBytes);
            }

            size_remain -= curCacheLineBytes;
            curCacheLineIdx++;

            if (size_remain <= 0) {
                if(isFree) {
                    if(current->getUsedBytes() == 0) {
                        current->lastWriterThreadIndex = -1;
                    }
                    if(current->falseSharingStatus != PASSIVE) {
                        if(current->lastFreeThreadIndex != ThreadLocalStatus::runningThreadIndex && current->lastFreeThreadIndex != -1) {
                            current->falseSharingStatus = PASSIVE;
                        }
                        current->lastAllocatingThreadIndex = -1;
                        current->lastFreeThreadIndex = ThreadLocalStatus::runningThreadIndex;
                    }
                } else {
                    if(current->falseSharingStatus != PASSIVE) {
                        if(current->lastAllocatingThreadIndex != ThreadLocalStatus::runningThreadIndex && current->lastAllocatingThreadIndex != -1) {
                            current->falseSharingStatus = ACTIVE;
                        }
                        current->lastFreeThreadIndex = -1;
                        current->lastAllocatingThreadIndex = ThreadLocalStatus::runningThreadIndex;
                    }
                }
            }
        }
		return true;
}

CacheMapEntry * PageMapEntry::getCacheMapEntry(bool mvBumpPtr) {
    if(!mvBumpPtr) {
        return cache_map_entry;
    }

    if(__builtin_expect(__atomic_load_n(&cache_map_entry, __ATOMIC_RELAXED) == NULL, 0)) {
        // Create a new page entries
        ShadowMemory::cache_map_lock.lock();
        if(__builtin_expect(__atomic_load_n(&cache_map_entry, __ATOMIC_RELAXED) == NULL, 1)) {
            __atomic_store_n(&cache_map_entry, ShadowMemory::doCacheMapBumpPointer(), __ATOMIC_RELAXED);
        }
        ShadowMemory::cache_map_lock.unlock();
    }


		return cache_map_entry;
}

bool CacheMapEntry::addUsedBytes(unsigned int num_bytes) {
    num_used_bytes += num_bytes;
		return true;
}

bool CacheMapEntry::subUsedBytes(unsigned int num_bytes) {
    num_used_bytes -= num_bytes;
		return true;
}

const char * boolToStr(bool p) {
		return (p ? "true" : "false");
}
