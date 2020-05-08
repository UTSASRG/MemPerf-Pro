#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include "shadowmemory.hh"
#include "real.hh"
#include "test/alloc.cpp"
#include "memwaste.h"

PageMapEntry ** ShadowMemory::mega_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_end = nullptr;
PageMapEntry * ShadowMemory::page_map_bump_ptr = nullptr;
CacheMapEntry * ShadowMemory::cache_map_begin = nullptr;
CacheMapEntry * ShadowMemory::cache_map_end = nullptr;
CacheMapEntry * ShadowMemory::cache_map_bump_ptr = nullptr;
pthread_spinlock_t ShadowMemory::cache_map_lock;
pthread_spinlock_t ShadowMemory::mega_map_lock;

eMapInitStatus ShadowMemory::isInitialized = E_MAP_INIT_NOT;

bool ShadowMemory::initialize() {
	if(isInitialized != E_MAP_INIT_NOT) {
			return false;
	}
	isInitialized = E_MAP_INIT_WORKING; 

	pthread_spin_init(&cache_map_lock, PTHREAD_PROCESS_PRIVATE);
	pthread_spin_init(&mega_map_lock, PTHREAD_PROCESS_PRIVATE);

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
    //fprintf(stderr, "updateObject address %p, size %d, isfree %d\n", address, size, isFree);
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

    // This call to getMegaMapEntry is needed, even though we only use the return value for the
    // print statement that immediately follows it; this is because it initializes the entry in
    // the megabyte map using the page map bump pointer if the entry is null. In other words,
    // do not comment out or remove this line.
    //PageMapEntry ** mega_entry = getMegaMapEntry(mega_index);
    //fprintf(stderr, "> mega_entry = %p, *mega_entry = %p, mega_index = %lu\n", mega_entry, *mega_entry, mega_index);

//    if(bibop) {
//        classSize = getClassSizeFor(size);
//        //fprintf(stderr, "bibop size = %d\n", size);
//    } else {
//        classSize = malloc_usable_size(address);
//    }
//
//    //fprintf(stderr, "updateObject, address = %p, classsize = %d\n", address, classSize);
//
//    if(classSize > MAX_OBJECT_SIZE) {
//        // Check to see if this is a large object (as we define the term);
//        // we will have no size class information if it is.
//        size = classSize;
//    } else if(isFree) {
//        // If this is a free operation, we will need the object's size (which is passed in as 0,
//        // and is thus useless to us).
//        size = getObjectSize(uintaddr, mega_index, firstPageIdx);
//    } else if((classSize - size) < OBJECT_SIZE_SENTINEL_SIZE) {
//        // If we are allocating an object that does not have enough internal capacity to store
//        // our object size sentinel value, we will bump up the size value to equal the
//        // class size. This prevents inconsistency problems caused by using the true size during
//        // allocation, and the class size during deallocation.
//        size = classSize;
//    }
		//unsigned firstPageOffset = (uintaddr & PAGESIZE_MASK);
		//unsigned firstCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
		//unsigned firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
    unsigned numNewPagesTouched = updatePages(uintaddr, mega_index, firstPageIdx, size, isFree);
    PageMapEntry::updateCacheLines(uintaddr, mega_index, firstPageIdx, size, isFree);
//		if(!isFree) {
//				updateObjectSize(uintaddr, size);
//		}

    return numNewPagesTouched;
}

unsigned ShadowMemory::updatePages(uintptr_t uintaddr, unsigned long mega_index, unsigned page_index, unsigned size, bool isFree) {
		// If this is an allocation request, we will also need the object's class size so that we may
		// store it in each PageMapEntry associated with each page of this object.
//		unsigned classSize;
//		if(bibop) {
//				classSize = getClassSizeFor(size);
//		} else {
//				classSize = malloc_usable_size((void *)uintaddr);
//		}

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
				//fprintf(stderr, ">   obj 0x%lx sz %u : current = %p, updating page %u, contrib size = %u\n",
				//				uintaddr, size, current, curPageIdx, curPageBytes);
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
						// Do not clear the page's class size on free -- this will be cleared
						// when the pages are re-utilized for use by a new object.
						//fprintf(stderr, ">   obj 0x%lx sz %u : first page : current=%p, mega_index=%lu, page index=%u, set class size=%u\n",
						//				uintaddr, size, current, mega_index, curPageIdx, classSize);
						//current->setClassSize(classSize);
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
						// Do not clear the page's class size on free -- this will be cleared
						// when the pages are re-utilized for use by a new object or mapping.
						//current->setClassSize(classSize);
				}
				size_remain -= curPageBytes;
				curPageIdx++;
		}

		return numNewPagesTouched;
}

//unsigned ShadowMemory::getPageClassSize(void * address) {
//		uintptr_t uintaddr = (uintptr_t)address;
//
//		unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
//		if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
//				fprintf(stderr, "ERROR: mega index of 0x%lx is too large: %lu > %u\n",
//								uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
//				abort();
//		}
//
//		unsigned page_index = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
//
//		return getPageClassSize(mega_index, page_index);
//}

//unsigned ShadowMemory::getPageClassSize(unsigned long mega_index, unsigned page_index) {
//		PageMapEntry * current = getPageMapEntry(mega_index, page_index);
//		return current->getClassSize();
//}

//unsigned ShadowMemory::getObjectSize(uintptr_t uintaddr, unsigned long mega_index, unsigned page_index) {
////		unsigned classSize;
////		if(isLibc) {
////				classSize = malloc_usable_size((void *)uintaddr);
////		} else {
////				classSize = getPageClassSize(mega_index, page_index);
////				//fprintf(stderr, "page_index = %d, classsize = %d\n", page_index, classSize);
////		}
////
////		// Check to see if this is a large object (as we define the term) -- we will have no size class info if it is.
////		if(classSize > MAX_OBJECT_SIZE) {
////				return classSize;
////		} else if(classSize == 0) {
////				fprintf(stderr, "ERROR: object %#lx has zero class size\n", uintaddr);
////				abort();
////		}
//
//    size_t size = MemoryWaste::getSize((void *)uintaddr);
//	//	if(size) {
//		    return (unsigned)size;
//
//	//	}
//		//uint32_t * ptrToSentinel = (uint32_t *)(uintaddr + classSize - OBJECT_SIZE_SENTINEL_SIZE);
//
////		if(((*ptrToSentinel) & OBJECT_SIZE_SENTINEL_MASK) == OBJECT_SIZE_SENTINEL) {
////				//unsigned retval = (*ptrToSentinel) & OBJECT_SIZE_MASK;
////				//fprintf(stderr, "***** retrieved %#lx %u\n", uintaddr, retval);
////				//fprintf(stderr, ">> object %#lx sentinel found at %p (val = 0x%x, enc size = %d)\n",
////				//        uintaddr, ptrToSentinel, *ptrToSentinel, ((*ptrToSentinel) & OBJECT_SIZE_MASK));
////				//return retval;
////				return (*ptrToSentinel) & OBJECT_SIZE_MASK;
////		} else {
//				//fprintf(stderr, ">> object %#lx no sentinel found at %p (val = 0x%x, enc size = %d), classSize = %zu\n",
//				//        uintaddr, ptrToSentinel, *ptrToSentinel, ((*ptrToSentinel) & OBJECT_SIZE_MASK), classSize);
//				//return classSize;
//		//}
//}

//unsigned ShadowMemory::getObjectSize(void * address) {
//
//		uintptr_t uintaddr = (uintptr_t)address;
//
//		// First compute the megabyte number of the given address.
//		unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
//		if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
//				fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
//								uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
//				abort();
//		}
//
//		unsigned page_index = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
//
//		return getObjectSize(uintaddr, mega_index, page_index);
//}

//bool ShadowMemory::updateObjectSize(uintptr_t uintaddr, unsigned size) {
//    //fprintf(stderr, "updateObjectSize uintaddr %p, size %d\n", uintaddr, size);
//    unsigned classSize;
//
//    if(bibop) {
//        classSize = getClassSizeFor(size);
//    } else {
//        classSize = malloc_usable_size((void *)uintaddr);
//    }
//
////    uint32_t * ptrToSentinel = (uint32_t *)(uintaddr + classSize - OBJECT_SIZE_SENTINEL_SIZE);
////    fprintf(stderr, "classsize = %d ptrToSentinel = %p\n", classSize, ptrToSentinel);
////    if((size <= MAX_OBJECT_SIZE) && (classSize - size >= OBJECT_SIZE_SENTINEL_SIZE)) {
////        *ptrToSentinel = (OBJECT_SIZE_SENTINEL | size);
////    } else {
////        *ptrToSentinel = 0;
////    }
//    fprintf(stderr, "Here\n");
//    return true;
//}

// Warning: the logic of this implementation is flawed -- occassionally, glibc will produce
// an object with a usable size 16 bytes greater than what is expected or calculated by
// this routine.
//size_t ShadowMemory::libc_malloc_usable_size(size_t size) {
//		if(size <= LIBC_MIN_OBJECT_SIZE) {
//				return LIBC_MIN_OBJECT_SIZE;
//		} else if(size >= large_object_threshold) {
//				return alignup(size, PAGESIZE);
//		}
//		unsigned next_mult_16 = ((~size & 0xf) + size + 1);
//		if(next_mult_16 - size >= LIBC_METADATA_SIZE) {
//				return (next_mult_16 - LIBC_METADATA_SIZE);
//		} else {
//				return (next_mult_16 + LIBC_METADATA_SIZE);
//		}
//}

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

		//fprintf(stderr, "%s(%#lx, %zu) alignup size = %zu\n", __FUNCTION__, uintaddr, length, alignup(length, PAGESIZE));
		length = alignup(length, PAGESIZE);


		unsigned curPageIdx;
		unsigned numPages = length >> LOG2_PAGESIZE;
		PageMapEntry * current;

		//fprintf(stderr, "> obj %#lx len %zu : numPages = %u, firstPageIdx = %u\n",
		//				uintaddr, length, numPages, firstPageIdx);

		// Next, loop until we have accounted for all mapping bytes...
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
    //fprintf(stderr, "curPageUsage %d, curCacheUsage %d\n", curPageUsage, curCacheUsage);
//    pid_t curThread = thrData.tid;
//    pid_t lineOwner = cme->getOwner();

    friendly_data * usageData = &thrData.friendlyData;
    /*
         typedef struct {
         unsigned long numAccesses;
         unsigned long numCacheWrites;
         unsigned long numCacheOwnerConflicts;
         unsigned long numCacheBytes;
         unsigned long numPageBytes;
         } friendly_data;
    */
		usageData->numAccesses++;
		usageData->numCacheBytes += curCacheUsage;
		usageData->numPageBytes += curPageUsage;

//		bool isCacheOwnerConflict = false;

		if(accessType == E_MEM_STORE) {
				usageData->numCacheWrites++;
//				if(lineOwner != curThread) {
//						isCacheOwnerConflict = true;
//						usageData->numCacheOwnerConflicts++;
//				}
				// change cache line owner
				//cme->setOwner(curThread);
		}

		// DEBUG OUTPUT
		//fprintf(stderr, "mem access : %s : addr=%#lx, curPageUsage=%u, curCacheUsage=%u, conflict?=%s\n",
		//				strAccessType, uintaddr, curPageUsage, curCacheUsage, boolToStr(isCacheOwnerConflict));
		//getchar();
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
//    fprintf(stderr, "%p, %p, %p, %p, %p, %p\n", uintaddr,
//           mega_index, (uintaddr & MEGABYTE_MASK), page_index, (uintaddr & PAGESIZE_MASK), cache_index);

    return {mega_index, page_index, cache_index};
}

void PageMapEntry::clear() {
		if(cache_map_entry) {
				size_t cache_entries_size = NUM_CACHELINES_PER_PAGE * sizeof(CacheMapEntry);
				memset(cache_map_entry, 0, cache_entries_size);
		}
		touched = false;
		num_used_bytes = 0;
		//classSize = 0;
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

void PageMapEntry::clearTouched() {
		touched = false;
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
				RealX::pthread_spin_lock(&mega_map_lock);
				if(__builtin_expect(__atomic_load_n(mega_entry, __ATOMIC_RELAXED) == NULL, 1)) {
						__atomic_store_n(mega_entry, doPageMapBumpPointer(), __ATOMIC_RELAXED);
				}
				RealX::pthread_spin_unlock(&mega_map_lock);
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
		__atomic_add_fetch(&num_used_bytes, num_bytes, __ATOMIC_RELAXED);
		return true;
}

bool PageMapEntry::subUsedBytes(unsigned int num_bytes) {
		__atomic_sub_fetch(&num_used_bytes, num_bytes, __ATOMIC_RELAXED);
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
				//fprintf(stderr, "> updateCacheLines: obj 0x%lx sz %u : current = %p, curCacheLineIdx = %u\n",
				//				uintaddr, size, current, curCacheLineIdx);
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
				//current->setOwner(thrData.tid);
				curCacheLineIdx++;
		}

		return true;
}

CacheMapEntry * PageMapEntry::getCacheMapEntry(bool mvBumpPtr) {
		if(mvBumpPtr && __builtin_expect(__atomic_load_n(&cache_map_entry, __ATOMIC_RELAXED) == NULL, 0)) {
				// Create a new entries
				RealX::pthread_spin_lock(&ShadowMemory::cache_map_lock);
				if(__builtin_expect(__atomic_load_n(&cache_map_entry, __ATOMIC_RELAXED) == NULL, 1)) {
						__atomic_store_n(&cache_map_entry, ShadowMemory::doCacheMapBumpPointer(), __ATOMIC_RELAXED);
				}
				RealX::pthread_spin_unlock(&ShadowMemory::cache_map_lock);
		}

		return cache_map_entry;
}

bool CacheMapEntry::addUsedBytes(unsigned int num_bytes) {
    __atomic_add_fetch(&num_used_bytes, num_bytes, __ATOMIC_RELAXED);
		return true;
}

bool CacheMapEntry::subUsedBytes(unsigned int num_bytes) {
		__atomic_sub_fetch(&num_used_bytes, num_bytes, __ATOMIC_RELAXED);
		return true;
}

const char * boolToStr(bool p) {
		return (p ? "true" : "false");
}
