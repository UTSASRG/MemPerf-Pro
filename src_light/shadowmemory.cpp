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

extern HashMap<uint64_t, CoherencyData, PrivateHeap> coherencyCaches;

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

//	fprintf(stderr, "MAX_PAGE_MAP_ENTRIES = %lu\n", MAX_PAGE_MAP_ENTRIES);

    coherencyCaches.initialize(HashFuncs::hash64Int, HashFuncs::compare64Int, NUM_COHERENCY_CACHES);


	isInitialized = true;

	return true;
}

void ShadowMemory::mallocUpdateObject(void * address, unsigned int size) {
    if(size == 0) {
        return;
    }

    uintptr_t uintaddr = (uintptr_t)address;

    uint64_t firstPageIdx = getPageIndex(uintaddr);
    mallocUpdatePages(uintaddr, firstPageIdx, size);

#ifdef CACHE_UTIL
    uint8_t firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
    uint8_t curCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
    PageMapEntry::mallocUpdateCacheLines(firstPageIdx, curCacheLineIdx, firstCacheLineOffset, size);
#endif

}

void ShadowMemory::freeUpdateObject(void * address, unsigned int size) {
    if(size == 0) {
        return;
    }

    uintptr_t uintaddr = (uintptr_t)address;

    uint64_t firstPageIdx = getPageIndex(uintaddr);
    freeUpdatePages(uintaddr, firstPageIdx, size);

#ifdef CACHE_UTIL
    uint8_t firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
    uint8_t curCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
    PageMapEntry::freeUpdateCacheLines(firstPageIdx, curCacheLineIdx, firstCacheLineOffset, size);
#endif

}

void ShadowMemory::mallocUpdatePages(uintptr_t uintaddr, uint64_t page_index, int64_t size) {

    unsigned short firstPageOffset = uintaddr & PAGESIZE_MASK;
    unsigned int curPageBytes = MIN(PAGESIZE - firstPageOffset, size);
    PageMapEntry * current = getPageMapEntry(page_index);

    current->addUsedBytes(curPageBytes);
    if(!current->isTouched()) {
        current->setTouched();
    }
    size -= curPageBytes;

    while(size >= PAGESIZE) {
        current++;
        current->setFull();
        if(!current->isTouched()) {
            current->setTouched();
        }
        size -= PAGESIZE;
    }

    if(size > 0) {
        current++;
        current->addUsedBytes(size);
        if(!current->isTouched()) {
            current->setTouched();
        }
    }
}

void ShadowMemory::freeUpdatePages(uintptr_t uintaddr, uint64_t page_index, int64_t size) {

    unsigned short firstPageOffset = uintaddr & PAGESIZE_MASK;
    unsigned int curPageBytes = MIN(PAGESIZE - firstPageOffset, size);
    PageMapEntry * current = getPageMapEntry(page_index);

    current->subUsedBytes(curPageBytes);
    size -= curPageBytes;

    while(size >= PAGESIZE) {
        current++;
        current->setEmpty();
        size -= PAGESIZE;
    }

    if(size > 0) {
        current++;
        current->subUsedBytes(size);
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

void HashLocksSetForCoherency::lock(uint64_t index) {
    size_t hashKey = HashFuncs::hash64Int(index, sizeof(uint64_t)) & (NUM_COHERENCY_CACHES-1);
    locks[hashKey].lock();
}

void HashLocksSetForCoherency::unlock(uint64_t index) {
    size_t hashKey = HashFuncs::hash64Int(index, sizeof(uint64_t)) & (NUM_COHERENCY_CACHES-1);
    locks[hashKey].unlock();
}

HashLocksSetForCoherency ShadowMemory::hashLocksSetForCoherency;
short lastThread = -1;

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

    if(lastThread != -1 && lastThread != ThreadLocalStatus::runningThreadIndex) {
        ThreadLocalStatus::friendlinessStatus.numThreadSwitch++;
        //        ThreadLocalStatus::friendlinessStatus.numOfSampledStoringInstructions++;
        uint64_t cacheIndex = uintaddr >> LOG2_CACHELINE_SIZE;
        uint8_t wordIndex = (uint8_t)((uintaddr & CACHELINE_SIZE_MASK) >> 3);
        CoherencyData * status = coherencyCaches.find(cacheIndex, sizeof(uint64_t));
        if(status == nullptr) {
            if(accessType == E_MEM_STORE) {
                hashLocksSetForCoherency.lock(cacheIndex);
                status = coherencyCaches.insert(cacheIndex, sizeof(uint64_t),
                                       CoherencyData{wordIndex, ThreadLocalStatus::runningThreadIndex, 0, 0, 1, {0}});
                hashLocksSetForCoherency.unlock(cacheIndex);
            }
        } else {
            if(accessType == E_MEM_STORE || status->ts >= 5 || status->fs >= 5) {
                if (status->word == wordIndex && status->tid != ThreadLocalStatus::runningThreadIndex) {
                    ThreadLocalStatus::friendlinessStatus.numOfTrueSharing++;
                    status->tid = ThreadLocalStatus::runningThreadIndex;
                    status->ts++;
                } else if (status->word != wordIndex) {
                    if (status->tid != ThreadLocalStatus::runningThreadIndex) {
                        ThreadLocalStatus::friendlinessStatus.numOfFalseSharing++;
                        status->word = wordIndex;
                        status->tid = ThreadLocalStatus::runningThreadIndex;
                        status->fs++;
                    } else {
                        status->word = wordIndex;
                    }
                }

            }
            status->time++;
        }

        if(status && (accessType == E_MEM_STORE || status->ts >= 5 || status->fs >= 5)) {
            uint8_t i;
            for (i = 0; i < 16 && status->tidsPerWord[wordIndex][i] &&
                        status->tidsPerWord[wordIndex][i] != ThreadLocalStatus::runningThreadIndex; ++i) { ; }
            if (i < 16 && status->tidsPerWord[wordIndex][i] != ThreadLocalStatus::runningThreadIndex) {
                status->tidsPerWord[wordIndex][i] = ThreadLocalStatus::runningThreadIndex;
            }
        }
    }
    lastThread = ThreadLocalStatus::runningThreadIndex;
}

extern HashMap<void*, uint32_t, PrivateHeap> objStatusMap;

void ShadowMemory::printOutput() {
    fprintf(ProgramStatus::outputFile, "\n");
    for(auto entry: coherencyCaches) {
        uint64_t cacheIndex = entry.getKey();
        CoherencyData * status = entry.getValue();
        if(status->time < 10) {
            continue;
        }
        if(status->ts * 100 / status->time > 15) {
            fprintf(ProgramStatus::outputFile, "%p: %u %u%% Application True Sharing\n", (void*)(cacheIndex<<LOG2_CACHELINE_SIZE), status->ts, status->ts*100/status->time);

            for(uint8_t i = 0; i < 8; ++i) {
                if(status->tidsPerWord[i][0]) {
                    fprintf(ProgramStatus::outputFile, "Word %u: ", i);
                    for(uint8_t j = 0; j < 16 && status->tidsPerWord[i][j]; ++j) {
                        fprintf(ProgramStatus::outputFile, "%d ", status->tidsPerWord[i][j]);
                    }
                    fprintf(ProgramStatus::outputFile, "\n");
                }
            }
        }
        if(status->fs * 100 / status->time > 15) {
            uint64_t cacheStart = cacheIndex << LOG2_CACHELINE_SIZE;
            uint64_t cacheEnd = cacheStart + CACHELINE_SIZE - 1;
            bool multiObj = false;
            bool foundObj = false;
            for(auto obj: objStatusMap) {
                uint64_t start = (uint64_t)obj.getKey();
                uint32_t size = *(obj.getValue());
                uint64_t end = start + size - 1;
                if(cacheStart <= start && start <= cacheEnd || cacheStart <= end && end <= cacheEnd) {
                    if(foundObj) {
                        multiObj = foundObj;
                        break;
                    }
                    foundObj = true;
                }
            }

            if(multiObj) {
                fprintf(ProgramStatus::outputFile, "%p: %u %u%% Allocator False Sharing\n", (void*)(cacheIndex<<LOG2_CACHELINE_SIZE), status->fs, status->fs*100/status->time);
            } else {
                fprintf(ProgramStatus::outputFile, "%p: %u %u%% Application False Sharing\n", (void*)(cacheIndex<<LOG2_CACHELINE_SIZE), status->fs, status->fs*100/status->time);
            }

            for(uint8_t i = 0; i < 8; ++i) {
                if(status->tidsPerWord[i][0]) {
                    fprintf(ProgramStatus::outputFile, "Word %u: ", i);
                    for(uint8_t j = 0; j < 16 && status->tidsPerWord[i][j]; ++j) {
                        fprintf(ProgramStatus::outputFile, "%d ", status->tidsPerWord[i][j]);
                    }
                    fprintf(ProgramStatus::outputFile, "\n");
                }
            }
        }
    }
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
void PageMapEntry::mallocUpdateCacheLines(uint64_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, unsigned int size) {
    int64_t size_remain = size;
    PageMapEntry * targetPage = ShadowMemory::getPageMapEntry(getPageIndex(uintaddr));
    CacheMapEntry * current = targetPage->getCacheMapEntry(true) + cache_index;

    uint8_t curCacheLineBytes = MIN(CACHELINE_SIZE - firstCacheLineOffset, size_remain);
    current->addUsedBytes(curCacheLineBytes);

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

        current->setFull();
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
        current->addUsedBytes(size_remain);
    }

}

void PageMapEntry::updateCacheLines(uint64_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, unsigned int size) {
    int64_t size_remain = size;
    PageMapEntry * targetPage = ShadowMemory::getPageMapEntry(getPageIndex(uintaddr));
    CacheMapEntry * current = targetPage->getCacheMapEntry(true) + cache_index;

    uint8_t curCacheLineBytes = MIN(CACHELINE_SIZE - firstCacheLineOffset, size_remain);
    current->subUsedBytes(curCacheLineBytes);

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

        current->setEmpty();
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
        current->subUsedBytes(size_remain);
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

#endif
