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
RangeOfHugePages ShadowMemory::HPBeforeInit;
RangeOfHugePages ShadowMemory::THPBeforeInit;
void * ShadowMemory::maxAddress;
void * ShadowMemory::minAddress;


void RangeOfHugePages::add(uintptr_t retval, size_t length) {
    retvals[num] = retval;
    lengths[num] = length;
    num++;
}


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
        abort();			// temporary, remove and replace with return false after testing
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

	minAddress = (void *)((uint64_t) 0 - (uint64_t)1);
	maxAddress = 0;

	isInitialized = true;

	return true;
}

size_t ShadowMemory::updateObject(void * address, size_t size, bool isFree) {
    if(address == nullptr || size == 0) {
        return 0;
    }

    minAddress = MIN(minAddress, address);
    maxAddress = MAX(maxAddress, (void *)((uint64_t)address+(uint64_t)size));

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
    unsigned firstPageOffset;
    unsigned int curPageBytes;
    unsigned numNewPagesTouched = 0;
    int size_remain = size;
    PageMapEntry * current;

    curPageIdx = page_index;
    // First test to determine whether this object begins on a page boundary. If not, then we must
    // increment the used bytes field of the first page separately.

    //Huge Page Check
    bool hugePageTouched = false;

#if (defined(ENABLE_HP) || defined(ENABLE_THP))
    hugePageTouched = (getPageMapEntry(mega_index/2*2, 0)->hugePage) || inHPInitRange((void *)uintaddr);
    if(hugePageTouched) {
        for(unsigned long m = mega_index/2*2; m <= (mega_index+(alignup(size, PAGESIZE_HUGE)/ONE_MB)-1)/2*2+1; m+=2) {
            current = getPageMapEntry(m, 0);
            if(!current->isTouched()) {
                current->setTouched();
                numNewPagesTouched += 512;
                if(current->donatedBySyscall) {
                    Predictor::faultedPages++;
                }
            }
        }
    }
#endif

    firstPageOffset = (uintaddr & PAGESIZE_MASK);

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
            if(!hugePageTouched && !current->isTouched()) {
                current->setTouched();
                numNewPagesTouched++;
                if(current->donatedBySyscall) {
                    Predictor::faultedPages++;
                }
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
        if((size_t)size_remain >= PAGESIZE) {
            curPageBytes = PAGESIZE;
        } else {
            curPageBytes = size_remain;
        }

        if(isFree) {
            current->subUsedBytes(curPageBytes);
        } else {
            current->addUsedBytes(curPageBytes);
            if(!hugePageTouched && !current->isTouched()) {
                current->setTouched();
                numNewPagesTouched++;
                if(current->donatedBySyscall) {
                    Predictor::faultedPages++;
                }
            }
        }
        size_remain -= curPageBytes;
        curPageIdx++;
    }

    return numNewPagesTouched;
}

void ShadowMemory::setHugePages(uintptr_t uintaddr, size_t length) {
#ifndef ENABLE_HP
    return;
#endif
    // First compute the megabyte number of the given address.
    unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
    if (mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
        fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
                uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
        abort();
    }
    length = alignup(length, PAGESIZE_HUGE);
    for(unsigned long m = mega_index/2*2; m <= (mega_index+(length/ONE_MB)-1)/2*2+1; m+=2) {
        PageMapEntry * current = getPageMapEntry(m, 0);
        current->hugePage = true;
    }
}

void ShadowMemory::cancelHugePages(uintptr_t uintaddr, size_t length) {
#ifndef ENABLE_HP
    return;
#endif
    // First compute the megabyte number of the given address.
    unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
    if (mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
        fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
                uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
        abort();
    }
    length = alignup(length, PAGESIZE_HUGE);
    for(unsigned long m = mega_index/2*2; m <= (mega_index+(length/ONE_MB)-1)/2*2+1; m+=2) {
        PageMapEntry * current = getPageMapEntry(m, 0);
        current->hugePage = false;
    }
}

void ShadowMemory::setTransparentHugePages(uintptr_t uintaddr, size_t length) {
#ifndef ENABLE_THP
    return;
#endif
    // First compute the megabyte number of the given address.
    unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
    if (mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
        fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
                uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
        abort();
    }
    size_t lengthForLoop = alignup(length, PAGESIZE_HUGE);
    bool hugePageHeadCompleted = false;
    for(unsigned long m = mega_index/2*2; m <= (mega_index+(lengthForLoop/ONE_MB)-1)/2*2+1; m+=2) {
        PageMapEntry * current = getPageMapEntry(m, 0);
        if(current->hugePage && !hugePageHeadCompleted) {
            length -= 2*ONE_MB;
        } else {
            hugePageHeadCompleted = true;
            if(length < 2*ONE_MB) {
                return;
            }
            current->hugePage = true;
        }
    }
}

size_t ShadowMemory::cleanupPages(uintptr_t uintaddr, size_t length) {
    unsigned numTouchedPages = 0;

    // First compute the megabyte number of the given address.
    unsigned long mega_index;
    unsigned firstPageIdx;

    mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
    if (mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
        fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
                uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
        abort();
    }
    firstPageIdx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);

    PageMapEntry * current;

    bool hugePageTouched = false;

#if (defined(ENABLE_HP) || defined(ENABLE_THP))
    hugePageTouched = getPageMapEntry(mega_index/2*2, 0)->hugePage || inHPInitRange((void *)uintaddr);
#endif

    if(hugePageTouched) {
        length = alignup(length, PAGESIZE_HUGE);
        for(unsigned long m = mega_index/2*2; m <= (mega_index+(length/ONE_MB)-1)/2*2+1; m+=2) {
            current = getPageMapEntry(m, 0);
            if(current->isTouched()) {
                numTouchedPages += 512;
            }
        }
    } else {
        length = alignup(length, PAGESIZE);
    }

    unsigned curPageIdx;
    unsigned numPages = length >> LOG2_PAGESIZE;

    for(curPageIdx = firstPageIdx; curPageIdx < firstPageIdx + numPages; curPageIdx++) {
        current = getPageMapEntry(mega_index, curPageIdx);
        if(current->isTouched()) {
            if(!hugePageTouched) {
                numTouchedPages++;
            }
            current->clear();
        }
    }
    return numTouchedPages*PAGESIZE;
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

    bool hugePageTouched = getPageMapEntry(tuple.mega_index/2*2, 0)->hugePage || inHPInitRange((void *)uintaddr);

    if(!pme->isTouched() && !hugePageTouched) {
        return;
    }

    if((cme = pme->getCacheMapEntry(false)) == NULL) {
            return;
        }

    cme += tuple.cache_index;


    ThreadLocalStatus::friendlinessStatus.recordANewSampling(cme->getUsedBytes(), pme->getUsedBytes());
    if(accessType == E_MEM_STORE) {
        ThreadLocalStatus::friendlinessStatus.numOfSampledStoringInstructions++;
        if(cme->lastWriterThreadIndex != ThreadLocalStatus::runningThreadIndex && cme->lastWriterThreadIndex != -1) {
            ThreadLocalStatus::friendlinessStatus.numOfSampledFalseSharingInstructions[cme->falseSharingStatus]++;
            if(!cme->falseSharingLineRecorded[cme->falseSharingStatus]) {
                ThreadLocalStatus::friendlinessStatus.numOfSampledFalseSharingCacheLines[cme->falseSharingStatus]++;
                cme->falseSharingLineRecorded[cme->falseSharingStatus] = true;
            }
        }
        cme->lastWriterThreadIndex = ThreadLocalStatus::runningThreadIndex;
    }
    if(!cme->sampled) {
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
    unsigned long mega_index;
    unsigned page_index;
    unsigned cache_index;

        mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
        if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
            fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
                    uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
            abort();
        }
        page_index = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
        cache_index = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);

    return {mega_index, page_index, cache_index};
}

void ShadowMemory::setHugePagesInit() {
#ifndef ENABLE_HP
    return;
#endif
    for(unsigned n = 0; n < HPBeforeInit.num; ++n) {
        setHugePages(HPBeforeInit.retvals[n], HPBeforeInit.lengths[n]);
    }
}

void ShadowMemory::setTransparentHugePagesInit() {
#ifndef ENABLE_THP
    return;
#endif
    for(unsigned  n = 0; n < THPBeforeInit.num; ++n) {
        setHugePages(THPBeforeInit.retvals[n], THPBeforeInit.lengths[n]);
    }
}

void PageMapEntry::clear() {

    if(cache_map_entry) {
        size_t cache_entries_size = NUM_CACHELINES_PER_PAGE * sizeof(CacheMapEntry);
        memset(cache_map_entry, 0, cache_entries_size);
    }

    donatedBySyscall = true;
    touched = false;
    num_used_bytes = 0;
    hugePage = false;
    clearBlowup();

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


PageMapEntry * ShadowMemory::getPageMapEntry(void * address) {
    uintptr_t uintaddr = (uintptr_t)address;
    unsigned long mega_idx = (uintaddr >> LOG2_MEGABYTE_SIZE);
    if(mega_idx > NUM_MEGABYTE_MAP_ENTRIES) {
        fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
                uintaddr, mega_idx, NUM_MEGABYTE_MAP_ENTRIES);
        abort();
    }
    unsigned page_idx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
    PageMapEntry ** mega_entry = getMegaMapEntry(mega_idx);
    return (*mega_entry + page_idx);
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

bool ShadowMemory::inHPInitRange(void * address) {
#ifdef ENABLE_HP
    for(unsigned n = 0; n < HPBeforeInit.num; ++n) {
        if((uint64_t)address >= HPBeforeInit.retvals[n] && (uint64_t)address <= HPBeforeInit.retvals[n] + HPBeforeInit.lengths[n]) {
            return true;
        }
    }
#endif
#ifdef ENABLE_THP
    for(unsigned n = 0; n < THPBeforeInit.num; ++n) {
        if((uint64_t)address >= THPBeforeInit.retvals[n] && (uint64_t)address <= THPBeforeInit.retvals[n] + THPBeforeInit.lengths[n]) {
            return true;
        }
    }
#endif
    return false;
}

void ShadowMemory::addBlowup(void *address, unsigned int classSizeIndex) {
    ShadowMemory::getPageMapEntry(address)->addBlowup(classSizeIndex);
}

void ShadowMemory::subBlowup(void *address, unsigned int classSizeIndex) {
    ShadowMemory::getPageMapEntry(address)->subBlowup(classSizeIndex);
}

void ShadowMemory::addFreeListNum(void *address, unsigned int classSizeIndex) {
    ShadowMemory::getPageMapEntry(address)->addFreeListNum(classSizeIndex);
}

void ShadowMemory::subFreeListNum(void *address, unsigned int classSizeIndex) {
    ShadowMemory::getPageMapEntry(address)->subFreeListNum(classSizeIndex);
}

void ShadowMemory::printAddressRange() {
    fprintf(stderr, "min: %p\nmax: %p\n", (void*)((uint64_t)minAddress>>LOG2_PAGESIZE_HUGE<<LOG2_PAGESIZE_HUGE), maxAddress);
}

void ShadowMemory::printAllPages() {
    uint64_t startPtr = (uint64_t)minAddress>>LOG2_PAGESIZE_HUGE<<LOG2_PAGESIZE_HUGE;
    uint64_t  endPtr = (uint64_t)maxAddress;
    uint64_t totalExFrag = 0;
    while (startPtr <= endPtr) {
        unsigned long mega_index = (startPtr >> LOG2_MEGABYTE_SIZE);
        bool hugePageTouched = getPageMapEntry(mega_index/2*2, 0)->hugePage || inHPInitRange((void *)startPtr);
        if(hugePageTouched) {
            bool touched = getPageMapEntry(mega_index/2*2, 0)->isTouched();
            if(touched) {
                uint64_t totalUsedBytes = 0;
                for(unsigned long m = mega_index/2*2; m <= mega_index/2*2+1; ++m) {
                    for(unsigned p = 0; p < NUM_PAGES_PER_MEGABYTE; ++p) {
                        PageMapEntry * current = getPageMapEntry(m, p);
                        totalUsedBytes += current->getUsedBytes();
                    }
                }
                if(PAGESIZE_HUGE-totalUsedBytes) {
                    fprintf(stderr, "%p - %p, huge page, %lu\n", (void*)startPtr, (void*)((uint64_t)startPtr+PAGESIZE_HUGE), PAGESIZE_HUGE-totalUsedBytes);
                    totalExFrag += PAGESIZE_HUGE-totalUsedBytes;
                }
            }
            startPtr += PAGESIZE_HUGE;
        } else {
            unsigned pageIdx = ((startPtr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
            bool touched = getPageMapEntry(mega_index, pageIdx)->isTouched();
            if(touched) {
                PageMapEntry * current = getPageMapEntry(mega_index, pageIdx);
                uint64_t totalUsedBytes = current->getUsedBytes();
                if(PAGESIZE-totalUsedBytes) {
                    fprintf(stderr, "%p - %p, %lu\n", (void*)startPtr, (void*)((uint64_t)startPtr+PAGESIZE), PAGESIZE-totalUsedBytes);
                    totalExFrag += PAGESIZE_HUGE-totalUsedBytes;
                }
            }
            startPtr += PAGESIZE;
        }
    }
    fprintf(stderr, "totalExFrag = %luK\n", totalExFrag/ONE_KB);
}

// Accepts relative page and cache indices -- for example, mega_idx=n, page_idx=257, cache_idx=65 would
// access mega_idx=n+1, page_idx=2, cache_idx=1.
CacheMapEntry * PageMapEntry::getCacheMapEntry(unsigned long mega_idx, unsigned page_idx, unsigned cache_idx) {

    unsigned target_cache_idx;
    unsigned calc_overflow_pages;

    target_cache_idx = cache_idx & CACHELINES_PER_PAGE_MASK;
    calc_overflow_pages = cache_idx >> LOG2_NUM_CACHELINES_PER_PAGE;

    unsigned rel_page_idx = page_idx + calc_overflow_pages;

    unsigned target_page_idx;
    unsigned calc_overflow_megabytes;

    target_page_idx = rel_page_idx & NUM_PAGES_PER_MEGABYTE_MASK;
    calc_overflow_megabytes = rel_page_idx >> LOG2_NUM_PAGES_PER_MEGABYTE;

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
    unsigned firstCacheLineIdx;

    firstCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);

    unsigned firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
    unsigned curCacheLineIdx;
    unsigned char curCacheLineBytes;
    int size_remain = size;
    CacheMapEntry *current;
    curCacheLineIdx = firstCacheLineIdx;
    if (firstCacheLineOffset) {
        current = getCacheMapEntry(mega_index, page_index, curCacheLineIdx);
        if (firstCacheLineOffset + size_remain >= CACHELINE_SIZE) {
            curCacheLineBytes = CACHELINE_SIZE - firstCacheLineOffset;
        } else {
            curCacheLineBytes = size_remain;
        }
        if (isFree) {
            current->subUsedBytes(curCacheLineBytes);
            if (current->getUsedBytes() == 0) {
                current->lastWriterThreadIndex = -1;
            }
            if (current->falseSharingStatus != PASSIVE) {
                if (current->lastFreeThreadIndex != ThreadLocalStatus::runningThreadIndex &&
                    current->lastFreeThreadIndex != -1) {
                    current->falseSharingStatus = PASSIVE;
                }
                current->lastAllocatingThreadIndex = -1;
                current->lastFreeThreadIndex = ThreadLocalStatus::runningThreadIndex;
            }
        } else {
            current->addUsedBytes(curCacheLineBytes);
            if (current->falseSharingStatus != PASSIVE) {
                if (current->lastAllocatingThreadIndex != ThreadLocalStatus::runningThreadIndex &&
                    current->lastAllocatingThreadIndex != -1) {
                    current->falseSharingStatus = ACTIVE;
                }
                current->lastFreeThreadIndex = -1;
                current->lastAllocatingThreadIndex = ThreadLocalStatus::runningThreadIndex;
            }
        }

        size_remain -= curCacheLineBytes;
        curCacheLineIdx++;

    }
    while (size_remain > 0) {
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
            if (isFree) {
                if (current->getUsedBytes() == 0) {
                    current->lastWriterThreadIndex = -1;
                }
                if (current->falseSharingStatus != PASSIVE) {
                    if (current->lastFreeThreadIndex != ThreadLocalStatus::runningThreadIndex &&
                        current->lastFreeThreadIndex != -1) {
                        current->falseSharingStatus = PASSIVE;
                    }
                    current->lastAllocatingThreadIndex = -1;
                    current->lastFreeThreadIndex = ThreadLocalStatus::runningThreadIndex;
                }
            } else {
                if (current->falseSharingStatus != PASSIVE) {
                    if (current->lastAllocatingThreadIndex != ThreadLocalStatus::runningThreadIndex &&
                        current->lastAllocatingThreadIndex != -1) {
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

BlowupNode * PageMapEntry::newBlowup(unsigned int classSizeIndex) {
    BlowupNode * newBlowupNode = (BlowupNode *)MyMalloc::shadowMalloc(sizeof(BlowupNode));
    newBlowupNode->classSizeIndex = classSizeIndex;
    newBlowupNode->blowupFlag = 0;
    newBlowupNode->freelistNum = 0;
    newBlowupNode->next = nullptr;
    return newBlowupNode;
}

void PageMapEntry::addBlowup(unsigned int classSizeIndex) {
    pagelock.lock();
    if(blowupList == nullptr) {
        blowupList = newBlowup(classSizeIndex);
        blowupList->blowupFlag++;
        pagelock.unlock();
        return;
    }
    BlowupNode * prevNode;
    BlowupNode * currentNode = blowupList;
    do {
        if(currentNode->classSizeIndex == classSizeIndex) {
            currentNode->blowupFlag++;
            pagelock.unlock();
            return;
        }
        prevNode = currentNode;
        currentNode = currentNode->next;
    } while (currentNode);

    prevNode->next = newBlowup(classSizeIndex);
    prevNode->next->blowupFlag++;
    pagelock.unlock();
}

void PageMapEntry::subBlowup(unsigned int classSizeIndex) {
    pagelock.lock();
    if(blowupList == nullptr) {
        pagelock.unlock();
        return;
    }

    BlowupNode * currentNode = blowupList;
    do {
        if(currentNode->classSizeIndex == classSizeIndex) {
            currentNode->blowupFlag--;
            pagelock.unlock();
            return;
        }
        currentNode = currentNode->next;
    } while (currentNode);
    pagelock.unlock();
}

void PageMapEntry::addFreeListNum(unsigned int classSizeIndex)  {
    pagelock.lock();
    if(blowupList == nullptr) {
        blowupList = newBlowup(classSizeIndex);
        blowupList->freelistNum++;
        pagelock.unlock();
        return;
    }
    BlowupNode * prevNode;
    BlowupNode * currentNode = blowupList;
    do {
        if(currentNode->classSizeIndex == classSizeIndex) {
            currentNode->freelistNum++;
            pagelock.unlock();
            return;
        }
        prevNode = currentNode;
        currentNode = currentNode->next;
    } while (currentNode);

    prevNode->next = newBlowup(classSizeIndex);
    prevNode->next->freelistNum++;
    pagelock.unlock();
}

void PageMapEntry::subFreeListNum(unsigned int classSizeIndex) {
    pagelock.lock();
    if(blowupList == nullptr) {
        pagelock.unlock();
        return;
    }

    BlowupNode * currentNode = blowupList;
    do {
        if(currentNode->classSizeIndex == classSizeIndex) {
            currentNode->freelistNum--;
            pagelock.unlock();
            return;
        }
        currentNode = currentNode->next;
    } while (currentNode);
    pagelock.unlock();
}

void PageMapEntry::clearBlowup() {
    pagelock.lock();
    if(blowupList == nullptr) {
        pagelock.unlock();
        return;
    }
    BlowupNode * currentNode = blowupList;
    do {
        if(currentNode->blowupFlag) {
            MemoryWaste::changeBlowup(currentNode->classSizeIndex, currentNode->blowupFlag);
        }
        if(currentNode->freelistNum) {
            MemoryWaste::changeFreelist(currentNode->classSizeIndex, currentNode->freelistNum);
        }
        currentNode = currentNode->next;
    } while (currentNode);
    blowupList = nullptr;
    pagelock.unlock();
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
