#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include "shadowmemory.hh"
#include "real.hh"
#include "objTable.h"
#include "spinlock.hh"
#include "threadlocalstatus.h"

PageMapEntry * ShadowMemory::page_map_begin = nullptr;
PageMapEntry * ShadowMemory::page_map_end = nullptr;
PageMapEntry * ShadowMemory::page_map_bump_ptr = nullptr;

#ifdef CACHE_UTIL
CacheMapEntry * ShadowMemory::cache_map_begin = nullptr;
CacheMapEntry * ShadowMemory::cache_map_end = nullptr;
CacheMapEntry * ShadowMemory::cache_map_bump_ptr = nullptr;
spinlock ShadowMemory::cache_map_lock;
#endif

bool ShadowMemory::isInitialized;

bool ShadowMemory::initialize() {

	if(isInitialized) {
	    return false;
	}

#ifdef CACHE_UTIL
    cache_map_lock.init();
#endif

	if((void *)(page_map_begin = (PageMapEntry *)mmap((void *)PAGE_MAP_START, PAGE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
        fprintf(stderr, "errno %d\n", ENOMEM);
        fprintf(stderr, "mmap of global page map failed. Adddress %lx size %lx error (%d) %s\n", PAGE_MAP_START, PAGE_MAP_SIZE, errno, strerror(errno));
        abort();
	}
	page_map_end = page_map_begin + MAX_PAGE_MAP_ENTRIES;
	page_map_bump_ptr = page_map_begin;

#ifdef CACHE_UTIL
	if((void *)(cache_map_begin = (CacheMapEntry *)mmap((void *)CACHE_MAP_START, CACHE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			perror("mmap of cache map region failed");
	}
	cache_map_end = cache_map_begin + MAX_CACHE_MAP_ENTRIES;
	cache_map_bump_ptr = cache_map_begin;
#endif

	fprintf(stderr, "MAX_PAGE_MAP_ENTRIES = %lu\n", MAX_PAGE_MAP_ENTRIES);

	isInitialized = true;

	return true;
}

void ShadowMemory::updateObject(void * address, unsigned int size, bool isFree) {
    if(address == nullptr || size == 0) {
        return;
    }

    uintptr_t uintaddr = (uintptr_t)address;

    uint64_t firstPageIdx = getPageIndex(uintaddr);
    updatePages(uintaddr, firstPageIdx, size, isFree);

#ifdef CACHE_UTIL
    uint8_t firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
    uint8_t curCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
    PageMapEntry::updateCacheLines(firstPageIdx, curCacheLineIdx, firstCacheLineOffset, size, isFree);
#endif

}

void ShadowMemory::updatePages(uintptr_t uintaddr, uint64_t page_index, int64_t size, bool isFree) {


    unsigned short firstPageOffset = uintaddr & PAGESIZE_MASK;

    unsigned int curPageBytes = MIN(PAGESIZE - firstPageOffset, size);
    PageMapEntry * current = getPageMapEntry(page_index);

    if(isFree) {
        current->subUsedBytes(curPageBytes);
    } else {
        current->addUsedBytes(curPageBytes);

        if(!current->isTouched()) {
            current->setTouched();
        }
    }
    size -= curPageBytes;

    while(size >= PAGESIZE) {
        current++;
        if(isFree) {
            current->setEmpty();
        } else {
            current->setFull();
            if(!current->isTouched()) {
                current->setTouched();
            }
        }
        size -= PAGESIZE;
    }

    if(size > 0) {
        current++;
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

    uint64_t pageIdx = getPageIndex(uintaddr);

    length = alignup(length, PAGESIZE);
    PageMapEntry * current =  getPageMapEntry(pageIdx);

    while(length) {
        if(current->isTouched()) {
            current->clear();
        }
        current++;
        length -= PAGESIZE;
    }
}

void ShadowMemory::doMemoryAccess(uintptr_t uintaddr, eMemAccessType accessType) {

    uint64_t pageIdx = getPageIndex(uintaddr);
    if(pageIdx >= MAX_PAGE_MAP_ENTRIES) {
        return;
    }

    uint8_t cacheIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
    PageMapEntry * pme = getPageMapEntry(pageIdx);

    if(!pme->isTouched() || pme->getUsedBytes() == 0) {
        return;
    }

#ifdef CACHE_UTIL
    CacheMapEntry * cme;

    if((cme = pme->getCacheMapEntry(false)) == nullptr) {
            return;
    }

    cme += tuple.cache_index;

    if(cme->getUsedBytes() == 0) {
        return;
    }
#endif

    ThreadLocalStatus::friendlinessStatus.cacheConflictDetector.hit(pageIdx, cacheIdx, ThreadLocalStatus::friendlinessStatus.numOfSampling);

#ifdef CACHE_UTIL
    ThreadLocalStatus::friendlinessStatus.recordANewSampling(cme->getUsedBytes(), pme->getUsedBytes());
#else
    ThreadLocalStatus::friendlinessStatus.recordANewSampling(pme->getUsedBytes());
#endif

}

void PageMapEntry::clear() {

#ifdef CACHE_UTIL
    if(cache_map_entry) {
        size_t cache_entries_size = NUM_CACHELINES_PER_PAGE * sizeof(CacheMapEntry);
        memset(cache_map_entry, 0, cache_entries_size);
    }
#endif

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

inline uint64_t ShadowMemory::getPageIndex(uint64_t addr) {
    return (addr - 0x550000000000UL) >> LOG2_PAGESIZE;
}

inline PageMapEntry * ShadowMemory::getPageMapEntry(uint64_t page_idx) {
    return page_map_begin + page_idx;
}

#ifdef CACHE_UTIL
inline CacheMapEntry * ShadowMemory::doCacheMapBumpPointer() {

    CacheMapEntry * curPtrValue = cache_map_bump_ptr;

    cache_map_bump_ptr += NUM_CACHELINES_PER_PAGE;

    if(cache_map_bump_ptr >= cache_map_end) {
        fprintf(stderr, "ERROR: cache map out of memory\n");
        abort();
    }
    return curPtrValue;
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

#ifdef CACHE_UTIL
void PageMapEntry::updateCacheLines(uint64_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, unsigned int size, bool isFree) {
    int64_t size_remain = size;
    PageMapEntry * targetPage = ShadowMemory::getPageMapEntry(getPageIndex(uintaddr));
    CacheMapEntry * current = targetPage->getCacheMapEntry(true) + cache_index;

    uint8_t curCacheLineBytes = MIN(CACHELINE_SIZE - firstCacheLineOffset, size_remain);
    current->updateCache(isFree, curCacheLineBytes);

    size_remain -= curCacheLineBytes;
    cache_index++;

    while (size_remain >= CACHELINE_SIZE) {

        if(cache_index == NUM_CACHELINES_PER_PAGE) {
            cache_index = 0;
            targetPage++;
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
            cache_index = 0;
            targetPage++;
            current = targetPage->getCacheMapEntry(true);
        } else {
            current++;
        }
        current->updateCache(isFree, size_remain);
    }

}

inline CacheMapEntry * PageMapEntry::getCacheMapEntry(bool mvBumpPtr) {
    if(!mvBumpPtr) {
        return cache_map_entry;
    }

    if(__builtin_expect(__atomic_load_n(&cache_map_entry, __ATOMIC_RELAXED) == NULL, 0)) {
        ShadowMemory::cache_map_lock.lock();
        if(__builtin_expect(__atomic_load_n(&cache_map_entry, __ATOMIC_RELAXED) == NULL, 1)) {
            __atomic_store_n(&cache_map_entry, ShadowMemory::doCacheMapBumpPointer(), __ATOMIC_RELAXED);
        }
        ShadowMemory::cache_map_lock.unlock();
    }

    return cache_map_entry;
}

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

void CacheMapEntry::updateCache(bool isFree, uint8_t num_bytes) {
    if (isFree) {
        subUsedBytes(num_bytes);
    } else {
        addUsedBytes(num_bytes);
    }
}
#endif
