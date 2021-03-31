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

	isInitialized = true;

	return true;
}

void ShadowMemory::updateObject(void * address, unsigned int size, bool isFree) {
//    fprintf(stderr, "%p, %u, %u\n", address, size, isFree);
    if(address == nullptr || size == 0) {
        return;
    }

    uintptr_t uintaddr = (uintptr_t)address;

    unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);

    if(mega_index > NUM_MEGABYTE_MAP_ENTRIES) {
        fprintf(stderr, "ERROR: mega index of 0x%lx too large: %lu > %u\n",
                uintaddr, mega_index, NUM_MEGABYTE_MAP_ENTRIES);
        abort();
    }
    uint8_t firstPageIdx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);
    updatePages(uintaddr, mega_index, firstPageIdx, size, isFree);
#ifdef OPEN_SAMPLING_EVENT
    uint8_t firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
    uint8_t curCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
    PageMapEntry::updateCacheLines(mega_index, firstPageIdx, curCacheLineIdx, firstCacheLineOffset, size, isFree);
#endif
}

void ShadowMemory::updatePages(uintptr_t uintaddr, unsigned long mega_index, uint8_t page_index, int64_t size, bool isFree) {


    unsigned short firstPageOffset = uintaddr & PAGESIZE_MASK;

    unsigned int curPageBytes = MIN(PAGESIZE - firstPageOffset, size);
    PageMapEntry ** mega_entry = getMegaMapEntry(mega_index);
    PageMapEntry * current = (*mega_entry + page_index);
    if(isFree) {
        current->subUsedBytes(curPageBytes);
    } else {
        current->addUsedBytes(curPageBytes);

        if(!current->isTouched()) {
            current->setTouched();
        }
    }
    size -= curPageBytes;
    page_index++;

    while(size >= PAGESIZE) {
        if(page_index == 0) {
            mega_index++;
            mega_entry = ShadowMemory::getMegaMapEntry(mega_index);
            current = *mega_entry;
        } else {
            current++;
        }


        if(isFree) {
            current->setEmpty();
        } else {
            current->setFull();
            if(!current->isTouched()) {
                current->setTouched();
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
            if(!current->isTouched()) {
                current->setTouched();
            }
        }
    }
}

void ShadowMemory::cleanupPages(uintptr_t uintaddr, size_t length) {

    unsigned long mega_index = (uintaddr >> LOG2_MEGABYTE_SIZE);
    unsigned pageIdx = ((uintaddr & MEGABYTE_MASK) >> LOG2_PAGESIZE);


        length = alignup(length, PAGESIZE);
    PageMapEntry * current =  *ShadowMemory::getMegaMapEntry(mega_index)+pageIdx;

    while(length) {
        if(current->isTouched()) {
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

    if(!pme->isTouched() || pme->getUsedBytes() == 0) {
        return;
    }

    if((cme = pme->getCacheMapEntry(false)) == nullptr) {
            return;
        }

    cme += tuple.cache_index;

#ifdef CACHE_UTIL

    if(cme->getUsedBytes() == 0) {
        return;
    }

#endif

    ThreadLocalStatus::friendlinessStatus.cacheConflictDetector.hit(tuple.mega_index, tuple.page_index, tuple.cache_index, ThreadLocalStatus::friendlinessStatus.numOfSampling);

    if(cme->lastWriterThreadIndex == 0) {
        ThreadLocalStatus::friendlinessStatus.numOfSampledCacheLines++;
        cme->lastWriterThreadIndex = 1;
    }

#ifdef CACHE_UTIL
    ThreadLocalStatus::friendlinessStatus.recordANewSampling(cme->getUsedBytes(), pme->getUsedBytes());
#else
    ThreadLocalStatus::friendlinessStatus.recordANewSampling(pme->getUsedBytes());
#endif

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

void PageMapEntry::clear() {

    if(cache_map_entry) {
        size_t cache_entries_size = NUM_CACHELINES_PER_PAGE * sizeof(CacheMapEntry);
        memset(cache_map_entry, 0, cache_entries_size);
    }

    touched = false;
    num_used_bytes = 0;
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

    uint8_t curCacheLineBytes = MIN(CACHELINE_SIZE - firstCacheLineOffset, size_remain);

#ifdef CACHE_UTIL
    current->updateCache(isFree, curCacheLineBytes);
#else
    current->updateCache(isFree);
#endif

    size_remain -= curCacheLineBytes;
    cache_index++;

#ifdef CACHE_UTIL
    while (size_remain >= CACHELINE_SIZE) {

        if(cache_index == NUM_CACHELINES_PER_PAGE) {
            page_index++;
            cache_index = 0;
            if(page_index == 0) {
                mega_index++;
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
    }
#else
    if(size_remain > 0 && size_remain % CACHELINE_SIZE) {
        uint64_t c = cache_index + size_remain / (CACHELINE_SIZE + 1);
        cache_index = c % NUM_CACHELINES_PER_PAGE;
        uint64_t p = page_index + c / NUM_CACHELINES_PER_PAGE;
        page_index = p % NUM_PAGES_PER_MEGABYTE;
        mega_index += p / NUM_PAGES_PER_MEGABYTE;

        mega_entry = ShadowMemory::getMegaMapEntry(mega_index);
        targetPage = (*mega_entry + page_index);
        current = targetPage->getCacheMapEntry(true) + cache_index;
        current->updateCache(isFree);
    }
#endif

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

#ifdef CACHE_UTIL
inline void CacheMapEntry::addUsedBytes(uint8_t num_bytes) {
    num_used_bytes += num_bytes;
}

inline void CacheMapEntry::subUsedBytes(uint8_t num_bytes) {
    num_used_bytes -= num_bytes;
}

inline void CacheMapEntry::setFull() {
    num_used_bytes = CACHELINE_SIZE;
}

inline void CacheMapEntry::setEmpty() {
    num_used_bytes = 0;
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
#endif

#ifdef CACHE_UTIL
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
#else
void CacheMapEntry::updateCache(bool isFree) {
    if (isFree) {
        if (getFS() == ACTIVE || getFS() == OBJECT) {
            if (lastAFThreadIndex != getThreadIndex() && lastAFThreadIndex >= 16) {
                setFS(PASSIVE);
            }
            lastAFThreadIndex = getThreadIndex();
        }
    } else {
        if (getFS() == OBJECT) {
            if (lastAFThreadIndex != getThreadIndex() && lastAFThreadIndex >= 16) {
                setFS(ACTIVE);
            }
            lastAFThreadIndex = getThreadIndex();
        }
    }
}
#endif

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
