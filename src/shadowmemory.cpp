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
#ifdef ENABLE_HP
RangeOfHugePages ShadowMemory::HPBeforeInit;
#ifdef ENABLE_THP
RangeOfHugePages ShadowMemory::THPBeforeInit;
#endif
#endif
#ifdef PRINT_MEM_DETAIL_THRESHOLD
void * ShadowMemory::maxAddress;
void * ShadowMemory::minAddress;
#endif

#ifdef ENABLE_HP
void RangeOfHugePages::add(uintptr_t retval, size_t length) {
    retvals[num] = retval;
    lengths[num] = length;
    num++;
}
#endif

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

#ifdef PRINT_MEM_DETAIL_THRESHOLD
	minAddress = (void *)((uint64_t) 0 - (uint64_t)1);
	maxAddress = 0;
#endif

	isInitialized = true;

	return true;
}

unsigned int ShadowMemory::updateObject(void * address, unsigned int size, bool isFree) {
    if(address == nullptr || size == 0) {
        return 0;
    }

#ifdef PRINT_MEM_DETAIL_THRESHOLD
    minAddress = MIN(minAddress, address);
    maxAddress = MAX(maxAddress, (void *)((uint64_t)address+(uint64_t)size));
#endif

    uintptr_t uintaddr = (uintptr_t)address;

    // First compute the megabyte number of the given address.

    unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);

    if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
        fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
                uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
        abort();
    }
    uint8_t firstPageIdx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
    unsigned int numNewPagesTouched = updatePages(uintaddr, mega_index, firstPageIdx, size, isFree);
    uint8_t firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
    uint8_t curCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
    PageMapEntry::updateCacheLines(mega_index, firstPageIdx, curCacheLineIdx, firstCacheLineOffset, size, isFree);

    return numNewPagesTouched * PAGESIZE;
}

unsigned int ShadowMemory::updatePages(uintptr_t uintaddr, unsigned long mega_index, uint8_t page_index, int64_t size, bool isFree) {

    unsigned int numNewPagesTouched = 0;
//    int64_t sizekeep = size;
//    uint8_t page_indexkeep = page_index;
//    unsigned long mega_indexkeep = mega_index;

    // First test to determine whether this object begins on a page boundary. If not, then we must
    // increment the used bytes field of the first page separately.

#if (defined(ENABLE_HP) || defined(ENABLE_THP))
    //Huge Page Check
    bool hugePageTouched = false;
    hugePageTouched = (getPageMapEntry(mega_index/2*2, 0)->hugePage) || inHPInitRange((void *)uintaddr);
    if(hugePageTouched) {
        for(unsigned long m = mega_index/2*2; m <= (mega_index+(alignup(size, PAGESIZE_HUGE)/ONE_MB)-1)/2*2+1; m+=2) {
            current = getPageMapEntry(m, 0);
            if(!current->isTouched()) {
                current->setTouched();
                numNewPagesTouched += 512;
                if(current->donatedBySyscall) {
                    current->donatedBySyscall = false;
                    Predictor::faultedPages++;
                }
            }
        }
    }
#endif

    unsigned short firstPageOffset = uintaddr & PAGESIZE_MASK;

//    if(size != 57) {
//        fprintf(stderr, "size = %lu at %u\n", size, firstPageOffset);
//    }

    unsigned int curPageBytes = MIN(PAGESIZE - firstPageOffset, size);
    PageMapEntry ** mega_entry = getMegaMapEntry(mega_index);
//    fprintf(stderr, "*mega_entry = %p\n", *mega_entry);
    PageMapEntry * current = (*mega_entry + page_index);
    if(isFree) {
        current->subUsedBytes(curPageBytes);
    } else {
        current->addUsedBytes(curPageBytes);

#if (defined(ENABLE_HP) || defined(ENABLE_THP))
        if(!hugePageTouched && !current->isTouched()) {
#else
        if(!current->isTouched()) {
#endif
            current->setTouched();
//            fprintf(stderr, "touch a new page, %lu, %lu, %u, %u\n", size, mega_index, page_index, firstPageOffset);
            numNewPagesTouched++;
            if(current->donatedBySyscall) {
                current->donatedBySyscall = false;
                Predictor::faultedPages++;
            }
        }
        else {
//            fprintf(stderr, "touch an old page, %lu, %lu, %u, %u\n", size, mega_index, page_index, firstPageOffset);
        }
    }
    size -= curPageBytes;
//        fprintf(stderr, "size = %ld, curPageBytes = %u\n", size, curPageBytes);
    page_index++;

    // Next, loop until we have accounted for all object bytes...
    while(size >= PAGESIZE) {
//        fprintf(stderr, "recur\n");
        if(page_index == 0) {
            mega_index++;
//            page_index = 0;
            mega_entry = ShadowMemory::getMegaMapEntry(mega_index);
            current = *mega_entry;
        } else {
            current++;
        }


        if(isFree) {
            current->setEmpty();
        } else {
            current->setFull();
#if (defined(ENABLE_HP) || defined(ENABLE_THP))
            if(!hugePageTouched && !current->isTouched()) {
#else
            if(!current->isTouched()) {
#endif
//                fprintf(stderr, "setTouch %p: %lu, %u\n", current, mega_index, page_index);
                current->setTouched();
//                fprintf(stderr, "touch a new page, %lu, %lu, %u\n", size, mega_index, page_index);
                numNewPagesTouched++;
                if(current->donatedBySyscall) {
                    current->donatedBySyscall = false;
                    Predictor::faultedPages++;
                }
            } else {
//                fprintf(stderr, "touch an old page, %lu, %lu, %u\n", size, mega_index, page_index);
//                fprintf(stderr, "Touched %p\n", current);
//                fprintf(stderr, "Touched %p: %lu, %u\n", current, mega_index, page_index);
            }
        }
        size -= PAGESIZE;
        page_index++;
    }

    if(size > 0) {
        if(page_index == 0) {
            mega_index++;
            current = *(ShadowMemory::getMegaMapEntry(mega_index));
        } else {
            current++;
        }

        if(isFree) {
            current->subUsedBytes(size);
        } else {
            current->addUsedBytes(size);
#if (defined(ENABLE_HP) || defined(ENABLE_THP))
            if(!hugePageTouched && !current->isTouched()) {
#else
            if(!current->isTouched()) {
#endif
                current->setTouched();
//                fprintf(stderr, "touch a new page, %lu, %lu, %u\n", size, mega_index, page_index);
                numNewPagesTouched++;
                if(current->donatedBySyscall) {
                    current->donatedBySyscall = false;
                    Predictor::faultedPages++;
                }
            }
//            else {
//                fprintf(stderr, "touched an old page, %lu, %lu, %u\n", size, mega_index, page_index);
//            }
        }
    }

//        fprintf(stderr, "obj addr = %p, size = %ld, midx = %lu, pidx = %u, offset = %u, pages = %u\n",
//                uintaddr, sizekeep, mega_indexkeep, page_indexkeep, firstPageOffset, numNewPagesTouched);

    return numNewPagesTouched;
}

#ifdef ENABLE_HP
void ShadowMemory::setHugePages(uintptr_t uintaddr, size_t length) {

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

#ifdef ENABLE_THP
void ShadowMemory::setTransparentHugePages(uintptr_t uintaddr, size_t length) {

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
#endif
#endif

size_t ShadowMemory::cleanupPages(uintptr_t uintaddr, size_t length) {
//        fprintf(stderr, "cleanupPages %p, %lu\n", uintaddr, length);
    unsigned numTouchedPages = 0;

    // First compute the megabyte number of the given address.
    unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
    unsigned pageIdx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
//    unsigned firstPageIdx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);


    /*
    if (mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
        fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
                uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
        abort();
    } */

#if (defined(ENABLE_HP) || defined(ENABLE_THP))
    bool hugePageTouched = false;
    hugePageTouched = getPageMapEntry(mega_index/2*2, 0)->hugePage || inHPInitRange((void *)uintaddr);
    if(hugePageTouched) {
        length = alignup(length, PAGESIZE_HUGE);
        for(unsigned long m = mega_index/2*2; m <= (mega_index+(length/ONE_MB)-1)/2*2+1; m+=2) {
            current = getPageMapEntry(m, 0);
            if(current->isTouched()) {
                numTouchedPages += 512;
            }
        }
    } else {
#endif
        length = alignup(length, PAGESIZE);
#if (defined(ENABLE_HP) || defined(ENABLE_THP))
    }
#endif

//    unsigned curPageIdx;
//    unsigned numPages = length >> LOG2_PAGESIZE;
    PageMapEntry * current =  *ShadowMemory::getMegaMapEntry(mega_index)+pageIdx;
//    PageMapEntry * current;

    while(length) {
        if(current->isTouched()) {
#if (defined(ENABLE_HP) || defined(ENABLE_THP))
            if(!hugePageTouched) {
#endif
                numTouchedPages++;
#if (defined(ENABLE_HP) || defined(ENABLE_THP))
            }
#endif
            current->clear();
        }
        pageIdx++;
        if(pageIdx == 0) {
            current = *ShadowMemory::getMegaMapEntry(++mega_index);
        } else {
            current++;
        }
        length -= PAGESIZE;
    }
/*
    for(curPageIdx = firstPageIdx; curPageIdx < firstPageIdx + numPages; curPageIdx++) {
        current = getPageMapEntry(mega_index, curPageIdx);
        if(current->isTouched()) {
#if (defined(ENABLE_HP) || defined(ENABLE_THP))
            if(!hugePageTouched) {
#endif
            numTouchedPages++;
#if (defined(ENABLE_HP) || defined(ENABLE_THP))
            }
#endif
            current->clear();
        }
    } */
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

#ifdef ENABLE_HP
    bool hugePageTouched = getPageMapEntry(tuple.mega_index/2*2, 0)->hugePage;
    hugePageTouched |= inHPInitRange((void *)uintaddr);
    if(!pme->isTouched() && !hugePageTouched) {
        return;
    }
#else
    if(!pme->isTouched()) {
        return;
    }
#endif

    if((cme = pme->getCacheMapEntry(false)) == nullptr) {
            return;
        }

    if(pme->getUsedBytes() == 0) {
        return;
    }

    cme += tuple.cache_index;

    if(cme->getUsedBytes() == 0) {
        return;
    }

//    fprintf(stderr, "hit cache %lu, %u, %u, %u at %p: %u\n", tuple.mega_index, tuple.page_index, tuple.cache_index, uintaddr & CACHELINE_SIZE_MASK, cme, cme->getUsedBytes());

    if(cme->lastWriterThreadIndex == 0) {
        ThreadLocalStatus::friendlinessStatus.numOfSampledCacheLines++;
        cme->lastWriterThreadIndex = 1;
    }

    ThreadLocalStatus::friendlinessStatus.recordANewSampling(cme->getUsedBytes(), pme->getUsedBytes());
    if(accessType == E_MEM_STORE) {
        ThreadLocalStatus::friendlinessStatus.numOfSampledStoringInstructions++;
        if(cme->lastWriterThreadIndex != cme->getThreadIndex() && cme->lastWriterThreadIndex >= 16) {
            ThreadLocalStatus::friendlinessStatus.numOfSampledFalseSharingInstructions[cme->getFS()]++;
            if(!cme->falseSharingLineRecorded[cme->getFS()]) {
                ThreadLocalStatus::friendlinessStatus.numOfSampledFalseSharingCacheLines[cme->getFS()]++;
                cme->falseSharingLineRecorded[cme->getFS()] = true;
            }
        }
        cme->lastWriterThreadIndex = cme->getThreadIndex();
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

    return {page_index, cache_index, mega_index};
}

#ifdef ENABLE_HP
void ShadowMemory::setHugePagesInit() {

    for(unsigned n = 0; n < HPBeforeInit.num; ++n) {
        setHugePages(HPBeforeInit.retvals[n], HPBeforeInit.lengths[n]);
    }
}
#ifdef ENABLE_THP
void ShadowMemory::setTransparentHugePagesInit() {

    for(unsigned  n = 0; n < THPBeforeInit.num; ++n) {
        setHugePages(THPBeforeInit.retvals[n], THPBeforeInit.lengths[n]);
    }
}
#endif
#endif

void PageMapEntry::clear() {

    if(cache_map_entry) {
        size_t cache_entries_size = NUM_CACHELINES_PER_PAGE * sizeof(CacheMapEntry);
        memset(cache_map_entry, 0, cache_entries_size);
    }

    donatedBySyscall = true;
    touched = false;
    num_used_bytes = 0;
#ifdef ENABLE_HP
    hugePage = false;
#endif
#ifdef ENABLE_PRECISE_BLOWUP
    clearBlowup();
#endif

}

uint8_t CacheMapEntry::getUsedBytes() {
    if(num_used_bytes > CACHELINE_SIZE) {
        return CACHELINE_SIZE;
    }
    if(num_used_bytes < 0) {
        return 0;
    }
    return num_used_bytes;

}

unsigned short PageMapEntry::getUsedBytes() {

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


inline PageMapEntry * ShadowMemory::getPageMapEntry(unsigned long mega_idx, unsigned page_idx) {

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

inline CacheMapEntry * ShadowMemory::doCacheMapBumpPointer() {

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

#ifdef ENABLE_HP
bool ShadowMemory::inHPInitRange(void * address) {
    for(unsigned n = 0; n < HPBeforeInit.num; ++n) {
        if((uint64_t)address >= HPBeforeInit.retvals[n] && (uint64_t)address <= HPBeforeInit.retvals[n] + HPBeforeInit.lengths[n]) {
            return true;
        }
    }
#ifdef ENABLE_THP
    for(unsigned n = 0; n < THPBeforeInit.num; ++n) {
        if((uint64_t)address >= THPBeforeInit.retvals[n] && (uint64_t)address <= THPBeforeInit.retvals[n] + THPBeforeInit.lengths[n]) {
            return true;
        }
    }
#endif
    return false;
}
#endif


#ifdef ENABLE_PRECISE_BLOWUP

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

#endif

#ifdef PRINT_MEM_DETAIL_THRESHOLD
void ShadowMemory::printAddressRange() {
    fprintf(stderr, "min: %p\nmax: %p\n", (void*)((uint64_t)minAddress>>LOG2_PAGESIZE_HUGE<<LOG2_PAGESIZE_HUGE), maxAddress);
}

void ShadowMemory::printAllPages() {
    uint64_t startPtr = (uint64_t)minAddress>>LOG2_PAGESIZE_HUGE<<LOG2_PAGESIZE_HUGE;
    uint64_t  endPtr = (uint64_t)maxAddress;
    uint64_t totalExFrag = 0;
    while (startPtr <= endPtr) {
        unsigned long mega_index = (startPtr >> LOG2_MEGABYTE_SIZE);
#ifdef ENABLE_HP
        bool hugePageTouched = getPageMapEntry(mega_index/2*2, 0)->hugePage;
        hugePageTouched |= inHPInitRange((void *)startPtr);
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
#endif
            uint64_t totalUsedBytesIn2Mb = 0;
            for(unsigned pageIdx = 0; pageIdx < 512; ++pageIdx) {
                bool touched = getPageMapEntry(mega_index, pageIdx)->isTouched();
                if(touched) {
                    PageMapEntry * current = getPageMapEntry(mega_index, pageIdx);
                    uint64_t totalUsedBytes = current->getUsedBytes();
                    if(PAGESIZE-totalUsedBytes) {
                        fprintf(stderr, "%p - %p, %lu\n", (void*)startPtr, (void*)((uint64_t)startPtr+PAGESIZE), PAGESIZE-totalUsedBytes);
                        totalExFrag += PAGESIZE-totalUsedBytes;
                        totalUsedBytesIn2Mb += PAGESIZE-totalUsedBytes;
                    }
                }
                startPtr += PAGESIZE;
            }
            if(totalUsedBytesIn2Mb) {
                fprintf(stderr, "-------%lu-------\n", totalUsedBytesIn2Mb);
            }
#ifdef ENABLE_HP
        }
#endif
    }
    fprintf(stderr, "totalExFrag = %luK\n", totalExFrag/ONE_KB);
}
#endif


void PageMapEntry::addUsedBytes(unsigned short num_bytes) {
    num_used_bytes += num_bytes;
}

void PageMapEntry::subUsedBytes(unsigned short num_bytes) {
    num_used_bytes -= num_bytes;
}

void PageMapEntry::setEmpty() {
        num_used_bytes = 0;
    }

void PageMapEntry::setFull() {
    num_used_bytes = PAGESIZE;
}

void PageMapEntry::updateCacheLines(unsigned long mega_index, uint8_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, unsigned int size, bool isFree) {
    int64_t size_remain = size;
    PageMapEntry ** mega_entry = ShadowMemory::getMegaMapEntry(mega_index);
    PageMapEntry * targetPage = (*mega_entry + page_index);
    CacheMapEntry * current = targetPage->getCacheMapEntry(true) + cache_index;

//    if(size == 48) {
//        if(isFree) {
//            fprintf(stderr, "free object %u\n", size);
//        } else {
//            fprintf(stderr, "alloc object %u\n", size);
//        }
//    }

//    if (firstCacheLineOffset) {
        uint8_t curCacheLineBytes = MIN(CACHELINE_SIZE - firstCacheLineOffset, size_remain);
        current->updateCache(isFree, curCacheLineBytes);
        size_remain -= curCacheLineBytes;
//    } else {
//        if (isFree) {
//            current->setEmpty();
//        } else {
//            current->setFull();
//        }
//        size_remain -= CACHELINE_SIZE;
//    }
//
//if(size == 48) {
//    fprintf(stderr, "first cache = %lu, %u, %u, %u at %p: %u\n", mega_index, page_index, cache_index, firstCacheLineOffset, current, current->getUsedBytes());
//}

    cache_index++;

    while (size_remain >= CACHELINE_SIZE) {

        if(cache_index == NUM_CACHELINES_PER_PAGE) {
            page_index++;
            cache_index = 0;
            if(page_index == 0) {
                mega_index++;
//                page_index = 0;
                mega_entry = ShadowMemory::getMegaMapEntry(mega_index);
                targetPage = *mega_entry;
            } else {
                targetPage++;
            }
            current = targetPage->getCacheMapEntry(true);
        } else {
            current++;
        }


        if (isFree) {
            current->setEmpty();
        } else {
            current->setFull();
        }

//        if(size == 48) {
//            fprintf(stderr, "middle cache = %lu, %u, %u at %p: %u\n", mega_index, page_index, cache_index, current,
//                    current->getUsedBytes());
//        }

        size_remain -= CACHELINE_SIZE;
        cache_index++;
    }

    if(size_remain > 0) {
        if(cache_index == NUM_CACHELINES_PER_PAGE) {
            page_index++;
            cache_index = 0;
            if(page_index == 0) {
                mega_index++;
                targetPage = *(ShadowMemory::getMegaMapEntry(mega_index));
            } else {
                targetPage++;
            }
            current = targetPage->getCacheMapEntry(true);
        } else {
            current++;
        }

        current->updateCache(isFree, size_remain);
//        if(size == 48) {
//            fprintf(stderr, "last cache = %lu, %u, %u, %u at %p: %u\n", mega_index, page_index, cache_index,
//                    size_remain, current, current->getUsedBytes());
//        }
    }
}

inline CacheMapEntry * PageMapEntry::getCacheMapEntry(bool mvBumpPtr) {
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

#ifdef ENABLE_PRECISE_BLOWUP

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

#endif

inline void CacheMapEntry::addUsedBytes(uint8_t num_bytes) {
    num_used_bytes += num_bytes;
}

inline void CacheMapEntry::subUsedBytes(uint8_t num_bytes) {
    num_used_bytes -= num_bytes;
}

void CacheMapEntry::updateCache(bool isFree, uint8_t num_bytes) {
    if (isFree) {
        subUsedBytes(num_bytes);
        if (getFS() == ACTIVE || getFS() == OBJECT) {
            if (lastAFThreadIndex != getThreadIndex() && lastAFThreadIndex >= 16) {
                setFS(PASSIVE);
            }
            lastAFThreadIndex = getThreadIndex();
        }
    } else {
        addUsedBytes(num_bytes);
        if (getFS() == OBJECT) {
            if (lastAFThreadIndex != getThreadIndex() && lastAFThreadIndex >= 16) {
                setFS(ACTIVE);
            }
            lastAFThreadIndex = getThreadIndex();
        }
    }
}

inline void CacheMapEntry::setFull() {
    num_used_bytes = CACHELINE_SIZE;
}

inline void CacheMapEntry::setEmpty() {
    num_used_bytes = 0;
}

inline void CacheMapEntry::setFS(FalseSharingType falseSharingType) {
    if(falseSharingType == OBJECT) {
        falseSharingStatus[0] = false;
        falseSharingStatus[1] = false;
    } else if(falseSharingType == ACTIVE) {
        falseSharingStatus[0] = true;
        falseSharingStatus[1] = false;
    } else {
        falseSharingStatus[0] = false;
        falseSharingStatus[1] = true;
    }
}

inline FalseSharingType CacheMapEntry::getFS() {
        if(falseSharingStatus[0] == false && falseSharingStatus[1] == false) {
            return OBJECT;
        } else if(falseSharingStatus[0] == true && falseSharingStatus[1] == false) {
            return ACTIVE;
        } else {
            return PASSIVE;
        }
}

    inline uint8_t CacheMapEntry::getThreadIndex() {
        return (uint8_t)((((uint16_t)ThreadLocalStatus::runningThreadIndex) & (uint16_t)15) | (uint16_t) 16);
    }
const char * boolToStr(bool p) {
		return (p ? "true" : "false");
}
