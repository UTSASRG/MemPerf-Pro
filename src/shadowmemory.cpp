#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include "shadowmemory.hh"
#include "real.hh"
#include "test/alloc.cpp"
#include "memwaste.h"
#include "spinlock.hh"

PageMapEntry ** ShadowMemory::mega_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_end = nullptr;
PageMapEntry * ShadowMemory::page_map_bump_ptr = nullptr;
CacheMapEntry * ShadowMemory::cache_map_begin = nullptr;
CacheMapEntry * ShadowMemory::cache_map_end = nullptr;
CacheMapEntry * ShadowMemory::cache_map_bump_ptr = nullptr;
//pthread_spinlock_t ShadowMemory::cache_map_lock;
//pthread_spinlock_t ShadowMemory::mega_map_lock;
spinlock ShadowMemory::cache_map_lock;
spinlock ShadowMemory::mega_map_lock;

eMapInitStatus ShadowMemory::isInitialized = E_MAP_INIT_NOT;

bool ShadowMemory::initialize() {
	if(isInitialized != E_MAP_INIT_NOT) {
			return false;
	}
	isInitialized = E_MAP_INIT_WORKING; 

//	pthread_spin_init(&cache_map_lock, PTHREAD_PROCESS_PRIVATE);
//	pthread_spin_init(&mega_map_lock, PTHREAD_PROCESS_PRIVATE);
cache_map_lock.init();
mega_map_lock.init();

	// Allocate 1MB-to-4KB mapping region
	if((void *)(mega_map_begin = (PageMapEntry **)mmap((void *)MEGABYTE_MAP_START, MEGABYTE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			fprintf(stderr, "mmap of global megabyte map failed. Adddress %lx error %s\n", MEGABYTE_MAP_START, strerror(errno));
//			abort();			// temporary, remove and replace with return false after testing
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
//			abort();			// temporary, remove and replace with return false after testing
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
				fprintf(stderr, "ERROR: null pointer passed into %s at %s:%d\n",
                __FUNCTION__, __FILE__, __LINE__);
        abort();
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

    return numNewPagesTouched;
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
				        //printf("sub %d\n", curPageBytes);
						current->subUsedBytes(curPageBytes);
				} else {
                    //printf("add %d\n", curPageBytes);
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

		length = alignup(length, PAGESIZE);


		unsigned curPageIdx;
		unsigned numPages = length >> LOG2_PAGESIZE;
		PageMapEntry * current;

		for(curPageIdx = firstPageIdx; curPageIdx < firstPageIdx + numPages; curPageIdx++) {
				current = getPageMapEntry(mega_index, curPageIdx);
				if(current->isTouched()) {
						numTouchedPages++;
				}
				current->clear();
		}

		return numTouchedPages;
}

void ShadowMemory::doMemoryAccess(uintptr_t uintaddr, eMemAccessType accessType) {

		// Only used for debug output (see end of function below)
		const char * strAccessType;
		switch(accessType) {
				case E_MEM_NONE:
						strAccessType = "none";
						break;
				case E_MEM_LOAD:
						strAccessType = "load";
						break;
				case E_MEM_STORE:
						strAccessType = "store";
						break;
				case E_MEM_PFETCH:
						strAccessType = "prefetch";
						break;
				case E_MEM_EXEC:
						strAccessType = "exec";
						break;
				case E_MEM_UNKNOWN:
						strAccessType = "unknown";
						break;
				default:
						strAccessType = "no_match";
		}

		PageMapEntry * pme;
		CacheMapEntry * cme;
		// We bypass checking and incrementing the MegaMap's bump pointer by not
		// calling the usual fetch functions (e.g., getMegaMapEntry, getPageMapEntry, etc.).
		// Thus, whenever the mega map entry is null/empty, we will simply give up
		// here (instead of creating one).
		map_tuple tuple = ShadowMemory::getMapTupleByAddress(uintaddr);
		PageMapEntry ** mega_entry = mega_map_begin + tuple.mega_index;
		if(__atomic_load_n(mega_entry, __ATOMIC_RELAXED) == NULL) {
				return;
		}
		// Calculate the PageMap entry based on the page index.
		pme = (*mega_entry + tuple.page_index);
		// If the page has not been touched before then we will leave.
		if(!pme->isTouched()) {
				return;
		}
		// Fetch the PageMapEntry's cache map entry pointer; if it is non-null, we will
		// add the appropriate cache index offset to it next.
		// The "false" boolean constant in the call to getCacheMapEntry(false) controls
		// whether the value of the CacheMap bump pointer is updated and returned
		// (rather than NULL) if there is no CacheMap entry for this address; we do not
		// wish to affect the bump pointer in this case, so this parameter should always
		// be set to false.
    if((cme = pme->getCacheMapEntry(false)) == NULL) {
            return;
        }
    cme += tuple.cache_index;

    unsigned int curPageUsage = pme->getUsedBytes();
    unsigned int curCacheUsage = cme->getUsedBytes();

    friendly_data * usageData = &thrData.friendlyData;

		usageData->numAccesses++;
		usageData->numCacheBytes += curCacheUsage;
		usageData->numPageBytes += curPageUsage;

//		bool isCacheOwnerConflict = false;
		if(accessType == E_MEM_STORE) {
				usageData->numCacheWrites++;
				if(cme->last_write != thrData.tid && cme->last_write != -1) {
				    if(cme->status == 0) {
                        usageData->numObjectFS++;
                        if(cme->objfs == false) {
                            usageData->numObjectFSCacheLine++;
                            cme->objfs = true;
                        }
				    } else if(cme->status == 1) {
                        usageData->numActiveFS++;
                        if(cme->actfs == false) {
                            usageData->numActiveFSCacheLine++;
                            cme->actfs = true;
                        }
				    } else {
                        usageData->numPassiveFS++;
                        if(cme->pasfs == false) {
                            usageData->numPassiveFSCacheLine++;
                            cme->pasfs = true;
                        }
				    }
				}
            cme->last_write = thrData.tid;
        }
		if(cme->sampled == false) {
            usageData->cachelines++;
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
				// Create a new page entries
//				RealX::pthread_spin_lock(&mega_map_lock);
            mega_map_lock.lock();
				if(__builtin_expect(__atomic_load_n(mega_entry, __ATOMIC_RELAXED) == NULL, 1)) {
						__atomic_store_n(mega_entry, doPageMapBumpPointer(), __ATOMIC_RELAXED);
				}
//				RealX::pthread_spin_unlock(&mega_map_lock);
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

bool PageMapEntry::updateCacheLines(uintptr_t uintaddr, unsigned long mega_index, unsigned page_index, unsigned size, bool isFree) {
		unsigned firstCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
		unsigned firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
		unsigned curCacheLineIdx;
		unsigned char curCacheLineBytes;
		int size_remain = size;
		CacheMapEntry * current;

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
				} else {
						current->addUsedBytes(curCacheLineBytes);
				}
				size_remain -= curCacheLineBytes;
				//current->setOwner(thrData.tid);
				curCacheLineIdx++;

				if(current->status != 2) {
				    if(!isFree) {
                        if(current->last_allocate != thrData.tid && current->last_allocate != -1) {
//                            if(current->status == 0) {
//                                current->status = 1;
//                            } else if(current->freed) {
//                                current->status = 2;
//                            }
                            if(current->freed) {
                                current->status = 2;
                            } else {
                                current->status = 1;
                            }
                        }
                        current->last_allocate = thrData.tid;
                        current->remain_size += curCacheLineBytes;
                        current->freed = false;
                    } else {
				        current->remain_size -= curCacheLineBytes;
				        if(current->remain_size <= 0) {
				            current->last_allocate = -1;
				            current->remain_size = 0;
				        } else {
				            current->freed = true;
				        }
				    }
				}

				if(current->getUsedBytes() == 0) {
                    current->status = 0;
                    current->last_write = -1;
				}

		}
		while(size_remain > 0) {
				current = getCacheMapEntry(mega_index, page_index, curCacheLineIdx);

				if(size_remain >= CACHELINE_SIZE) {
						curCacheLineBytes = CACHELINE_SIZE;
				} else {
						curCacheLineBytes = size_remain;
				}

				if(isFree) {
						current->subUsedBytes(curCacheLineBytes);
				} else {
						current->addUsedBytes(curCacheLineBytes);
				}

				size_remain -= curCacheLineBytes;
				curCacheLineIdx++;

				if(size_remain < 0) {
                    if(current->status != 2) {
                        if(!isFree) {
                            if(current->last_allocate != thrData.tid && current->last_allocate != -1) {
//                                if(current->status == 0) {
//                                    current->status = 1;
//                                } else if(current->freed) {
//                                    current->status = 2;
//                                }
                                if(current->freed) {
                                    current->status = 2;
                                } else {
                                    current->status = 1;
                                }
                            }
                            current->last_allocate = thrData.tid;
                            current->remain_size += curCacheLineBytes;
                            current->freed = false;
                        } else {
                            current->remain_size -= curCacheLineBytes;
                            if(current->remain_size <= 0) {
                                current->last_allocate = -1;
                                current->remain_size = 0;
                            } else {
                                current->freed = true;
                            }
                        }
                    }
				}

            if(current->getUsedBytes() == 0) {
                current->status = 0;
                current->last_write = -1;
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
//				RealX::pthread_spin_lock(&mega_map_lock);
        ShadowMemory::cache_map_lock.lock();
        if(__builtin_expect(__atomic_load_n(&cache_map_entry, __ATOMIC_RELAXED) == NULL, 1)) {
            __atomic_store_n(&cache_map_entry, ShadowMemory::doCacheMapBumpPointer(), __ATOMIC_RELAXED);
        }
//				RealX::pthread_spin_unlock(&mega_map_lock);
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
