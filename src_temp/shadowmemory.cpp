#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
//#include <iostream>
//#include <cstdio>
#include <algorithm>
#include <functional>

#include "shadowmemory.hh"
#include "real.hh"
#include "objTable.h"
#include "spinlock.hh"
#include "threadlocalstatus.h"

//extern HashMap<uint64_t, CoherencyData, PrivateHeap> coherencyCaches;
ConflictData conflictData;
uint8_t numOfCoherencyCaches;
spinlock lockForCoherencyCaches;
CoherencyData coherencyCaches[32];

//void * ShadowMemory::addressRanges[NUM_ADDRESS_RANGE] = {(void*)0x550000000000UL, (void*)0x7f0000000000UL};
void * ShadowMemory::addressRanges[NUM_ADDRESS_RANGE] =
        {(void*)0x80000000000UL, (void*)0x10000000000UL, (void*)0xa0000000000UL,
         (void*)0x120000000000UL, (void*)0x140000000000UL,
         (void*)0x560000000000UL, (void*)0x7f0000000000UL,
         (void*)0x550000000000UL};

//#ifdef UTIL
PageMapEntry * ShadowMemory::page_map_begin[NUM_ADDRESS_RANGE];
PageMapEntry * ShadowMemory::page_map_end[NUM_ADDRESS_RANGE];

//#ifdef CACHE_UTIL
CacheMapEntry * ShadowMemory::cache_map_begin = nullptr;
CacheMapEntry * ShadowMemory::cache_map_end = nullptr;
CacheMapEntry * ShadowMemory::cache_map_bump_ptr = nullptr;
spinlock ShadowMemory::cache_map_lock;
//#endif

//#endif

bool ShadowMemory::isInitialized;


bool ShadowMemory::initialize() {

	if(isInitialized) {
	    return false;
	}

//#ifdef UTIL

//#ifdef CACHE_UTIL
    cache_map_lock.init();
//#endif

	for(uint8_t i = 0; i < NUM_ADDRESS_RANGE; ++i) {
        if((void *)(page_map_begin[i] = (PageMapEntry *)mmap(addressRanges[i], PAGE_MAP_SIZE,
                                                          PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
            fprintf(stderr, "errno %d\n", ENOMEM);
            fprintf(stderr, "mmap of global page map failed. Adddress %p size %lx error (%d) %s\n", addressRanges[i], PAGE_MAP_SIZE, errno, strerror(errno));
            abort();
        }
        page_map_end[i] = page_map_begin[i] + MAX_PAGE_MAP_ENTRIES;
	}

//#ifdef CACHE_UTIL
	if((void *)(cache_map_begin = (CacheMapEntry *)mmap((void *)CACHE_MAP_START, CACHE_MAP_SIZE,
									PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
			perror("mmap of cache map region failed");
	}
	cache_map_end = cache_map_begin + MAX_CACHE_MAP_ENTRIES;
	cache_map_bump_ptr = cache_map_begin;
//#endif

//#endif
//	fprintf(stderr, "MAX_PAGE_MAP_ENTRIES = %lu\n", MAX_PAGE_MAP_ENTRIES);

//    coherencyCaches.initialize(HashFuncs::hash64Int, HashFuncs::compare64Int, NUM_COHERENCY_CACHES);


	isInitialized = true;

	return true;
}

void ShadowMemory::mallocUpdateObject(void * address, unsigned int size) {
    if(size == 0) {
        return;
    }

    uintptr_t uintaddr = (uintptr_t)address;

#ifdef ON_DEBUG
    fprintf(stderr, "size %u address %p\n", size, address);
#endif

    uint8_t firstPageRange;
    uint64_t firstPageIdx;
    getPageIndex(uintaddr, &firstPageRange, &firstPageIdx);

    if(firstPageRange == NUM_ADDRESS_RANGE) {
//        fprintf(stderr, "malloc address %p out of range\n", uintaddr);
        return;
    }

    mallocUpdatePages(uintaddr, firstPageRange, firstPageIdx, size);

#ifdef CACHE_UTIL
    uint8_t firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
    uint8_t curCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
    PageMapEntry::mallocUpdateCacheLines(firstPageRange, firstPageIdx, curCacheLineIdx, firstCacheLineOffset, size);
#endif

}

void ShadowMemory::freeUpdateObject(void * address, ObjStat objStat) {
    if(objStat.size == 0) {
        return;
    }

    uintptr_t uintaddr = (uintptr_t)address;

    uint8_t firstPageRange;
    uint64_t firstPageIdx;
    getPageIndex(uintaddr, &firstPageRange, &firstPageIdx);

    if(firstPageRange == NUM_ADDRESS_RANGE) {
//        fprintf(stderr, "free address %p out of range\n", uintaddr);
        return;
    }
#ifdef UTIL
    freeUpdatePages(uintaddr, firstPageRange, firstPageIdx, objStat.size);
#endif

//#ifdef CACHE_UTIL
    uint8_t firstCacheLineOffset = (uintaddr & CACHELINE_SIZE_MASK);
    uint8_t curCacheLineIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);
    PageMapEntry::freeUpdateCacheLines(firstPageRange, firstPageIdx, curCacheLineIdx, firstCacheLineOffset, objStat);
//#endif

}

void ShadowMemory::mallocUpdatePages(uintptr_t uintaddr, uint8_t range, uint64_t page_index, int64_t size) {

    unsigned short firstPageOffset = uintaddr & PAGESIZE_MASK;
    unsigned int curPageBytes = MIN(PAGESIZE - firstPageOffset, size);
    PageMapEntry * current = getPageMapEntry(range, page_index);

    current->addUsedBytes(curPageBytes);
    size -= curPageBytes;

    while(size >= PAGESIZE) {
        current++;
        current->setFull();
        size -= PAGESIZE;
    }

    if(size > 0) {
        current++;
        current->addUsedBytes(size);
    }
}

#ifdef UTIL
void ShadowMemory::freeUpdatePages(uintptr_t uintaddr, uint8_t range, uint64_t page_index, int64_t size) {

    unsigned short firstPageOffset = uintaddr & PAGESIZE_MASK;
    unsigned int curPageBytes = MIN(PAGESIZE - firstPageOffset, size);
    PageMapEntry * current = getPageMapEntry(range, page_index);

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

    uint8_t firstPageRange;
    uint64_t firstPageIdx;
    getPageIndex(uintaddr, &firstPageRange, &firstPageIdx);

    length = alignup(length, PAGESIZE);
    PageMapEntry * current =  getPageMapEntry(firstPageRange, firstPageIdx);

    while(length) {
        current->clear();
        current++;
        length -= PAGESIZE;
    }
}
#endif

//void HashLocksSetForCoherency::lock(uint64_t index) {
//    size_t hashKey = HashFuncs::hash64Int(index, sizeof(uint64_t)) & (NUM_COHERENCY_CACHES-1);
//    locks[hashKey].lock();
//}
//
//void HashLocksSetForCoherency::unlock(uint64_t index) {
//    size_t hashKey = HashFuncs::hash64Int(index, sizeof(uint64_t)) & (NUM_COHERENCY_CACHES-1);
//    locks[hashKey].unlock();
//}

//HashLocksSetForCoherency ShadowMemory::hashLocksSetForCoherency;
short lastThread = -1;

#ifdef OPEN_SAMPLING_EVENT

void ShadowMemory::doMemoryAccess(uintptr_t uintaddr, eMemAccessType accessType, bool miss) {

    uint8_t pageRange;
    uint64_t pageIdx;
    getPageIndex(uintaddr, &pageRange, &pageIdx);

//#ifdef UTIL
    if(pageRange == NUM_ADDRESS_RANGE) {
//        fprintf(stderr, " sampling address %p out of range\n", uintaddr);
        return;
    }
//#endif

    uint8_t cacheIdx = ((uintaddr & PAGESIZE_MASK) >> LOG2_CACHELINE_SIZE);

#ifdef ON_DEBUG
    fprintf(stderr, "pageIdx = %lu cacheIdx = %u addr = %p\n", pageIdx, cacheIdx, uintaddr);
#endif

    PageMapEntry * pme = getPageMapEntry(pageRange, pageIdx);

//#ifdef UTIL
    if(pme->getUsedBytes() == 0) {
        return;
    }
//#endif

    CacheMapEntry * cme;

    if((cme = pme->getCacheMapEntry(false)) == nullptr) {
        return;
    }

    cme += cacheIdx;

#ifdef CACHE_UTIL
    if(cme->getUsedBytes() == 0) {
        return;
    }
#endif

#ifdef UTIL

#ifdef CACHE_UTIL
    ThreadLocalStatus::friendlinessStatus.recordANewSampling(cme->getUsedBytes(), pme->getUsedBytes());
#else
    ThreadLocalStatus::friendlinessStatus.recordANewSampling(pme->getUsedBytes());
#endif

#else
    ThreadLocalStatus::friendlinessStatus.recordANewSampling();
#endif

    if(miss) {

        conflictData.addMiss(uintaddr, cme);

//        if(cme->lastThreadIdx && cme->lastThreadIdx != ThreadLocalStatus::runningThreadIndex) {
            if((cme->misses & (uint8_t)0x3f) >= 5) {
                CoherencyData * status = nullptr;
                if(cme->misses & (uint8_t)0x40) {
                    uint8_t i;
                    for(i = 0; i < numOfCoherencyCaches && coherencyCaches[i].cme != cme; ++i) {} /// find index
                    status = &(coherencyCaches[i]);
                    status->misses++;

                } else if (numOfCoherencyCaches < 32) {
                    lockForCoherencyCaches.lock();
//                    fprintf(stderr, "numOfCoherencyCaches = %u\n", numOfCoherencyCaches);
                    if (numOfCoherencyCaches < 32) {
                        status = &(coherencyCaches[numOfCoherencyCaches]);
                        numOfCoherencyCaches++;
                    }
                    lockForCoherencyCaches.unlock();
                    if(status) {
                        cme->misses |= (uint8_t)0x40;
                        status->cme = cme;
                        status->misses = 5;
                    }
                }

                if(status) {
                    uint8_t wordIndex = (uint8_t)((uintaddr & CACHELINE_SIZE_MASK) >> 3);
                    uint8_t j;
                    for (j = 0; j < 16 && status->tidsPerWord[wordIndex][j] &&
                                status->tidsPerWord[wordIndex][j] != ThreadLocalStatus::runningThreadIndex; ++j) { ; }
                    if (j < 16 && status->tidsPerWord[wordIndex][j] != ThreadLocalStatus::runningThreadIndex) {
                        status->tidsPerWord[wordIndex][j] = ThreadLocalStatus::runningThreadIndex;
                    }
                }

            } else {
                cme->misses++;
            }
        }
//        cme->lastThreadIdx = ThreadLocalStatus::runningThreadIndex;
//    }
}
#endif

bool cmp(const uint8_t a, const uint8_t b) {return a > b; }

void ShadowMemory::printOutput() {
    fprintf(stderr, "numOfCoherencyCaches = %u\n", numOfCoherencyCaches);
//    uint8_t tScores[32] = {0};
    uint8_t fScores[32] = {0};
//    uint8_t numOfTScores = 0;
    uint8_t numOfFScores = 0;
//    uint8_t totalTScore = 0;
    uint8_t totalFScore = 0;
    for(uint8_t i = 0; i < numOfCoherencyCaches; ++i) {
        CoherencyData * status = &(coherencyCaches[i]);
        uint8_t score = status->misses * 100 / conflictData.totalMisses;
        if(score) {
            if(status->allocateTid[1]) {
                fprintf(ProgramStatus::outputFile, "----------\nallocator cache %u%%, objects are allocated by thread %u and %u\n",
                        score, status->allocateTid[0], status->allocateTid[1]);
            } else {
                fprintf(ProgramStatus::outputFile, "----------\napplication cache %u%%\n", score);
            }

            Callsite::printCallSite(status->callKey[0]);
            Callsite::printCallSite(status->callKey[1]);

            bool trueSharing = false, falseSharing = false;
            uint16_t lastTid = 0;
            for (uint8_t i = 0; i < NUM_WORD; ++i) {
                uint8_t j;
                for (j = 0; j < 16 && status->tidsPerWord[i][j]; ++j) { ; }

                if(j >= 2) {
                    trueSharing = true;
                    status->cme->misses = 0;
                }
                if(lastTid == 0) {
                    lastTid = status->tidsPerWord[i][0];
                } else if(lastTid != status->tidsPerWord[i][0] || j >= 2) {
                    falseSharing = true;
                    status->cme->misses = 0;
                }
            }

            if(trueSharing) {
                fprintf(ProgramStatus::outputFile, "True Sharing\n");
//                tScores[numOfTScores++] += score;
            }
            if(falseSharing) {
                fprintf(ProgramStatus::outputFile, "False Sharing\n");
                if(status->allocateTid[1]) {
                    fScores[numOfFScores++] += score;
                }
            }
        }
    }

//    if(numOfTScores) {
//        std::sort(tScores, tScores+numOfTScores, cmp);
//        for(uint8_t i = 0, j = 1; i < numOfTScores; ++i, j*=2) {
//            totalTScore += tScores[i] / j;
//        }
//        fprintf(ProgramStatus::outputFile, "True Sharing Score = %u\n", totalTScore);
//    }

    if(numOfFScores) {
        std::sort(fScores, fScores+numOfFScores, cmp);
        for(uint8_t i = 0, j = 1; i < numOfFScores; ++i, j*=2) {
            totalFScore += fScores[i] / j;
        }
        fprintf(ProgramStatus::outputFile, "False Sharing Score = %u\n", totalFScore);
    }

    fprintf(ProgramStatus::outputFile, "\n");
    conflictData.printOutput();
    fprintf(ProgramStatus::outputFile, "\n");

}

#ifdef UTIL
void PageMapEntry::clear() {

//#ifdef CACHE_UTIL
    if(cache_map_entry) {
        size_t cache_entries_size = NUM_CACHELINES_PER_PAGE * sizeof(CacheMapEntry);
        memset(cache_map_entry, 0, cache_entries_size);
    }
//#endif
    num_used_bytes = 0;
}
#endif

unsigned short PageMapEntry::getUsedBytes() {

    if(num_used_bytes > PAGESIZE) {
        return PAGESIZE;
    }

    if(num_used_bytes < 0) {
        return 0;
    }
    return num_used_bytes;
}

void ShadowMemory::getPageIndex(uint64_t addr, uint8_t * range, uint64_t * index) {
    for(uint8_t i = 0; i < NUM_ADDRESS_RANGE; ++i) {
        if(addr >= (uint64_t)addressRanges[i]) {
           *index = (addr - (uint64_t)addressRanges[i]) >> LOG2_PAGESIZE;
           if(*index <= MAX_PAGE_MAP_ENTRIES) {
               *range = i;
               return;
           }
        }
    }
    *range = NUM_ADDRESS_RANGE;
}


inline PageMapEntry * ShadowMemory::getPageMapEntry(uint8_t range, uint64_t page_idx) {
    return page_map_begin[range] + page_idx;
}

//#ifdef CACHE_UTIL
inline CacheMapEntry * ShadowMemory::doCacheMapBumpPointer() {

    CacheMapEntry * curPtrValue = cache_map_bump_ptr;

    cache_map_bump_ptr += NUM_CACHELINES_PER_PAGE;

    if(cache_map_bump_ptr >= cache_map_end) {
        fprintf(stderr, "ERROR: cache map out of memory\n");
        abort();
    }
    return curPtrValue;
}
//#endif


void PageMapEntry::addUsedBytes(unsigned short num_bytes) {
    num_used_bytes += num_bytes;
}

#ifdef UTIL
void PageMapEntry::subUsedBytes(unsigned short num_bytes) {
    num_used_bytes -= num_bytes;
}

void PageMapEntry::setEmpty() {
    num_used_bytes = 0;
}
#endif

inline void PageMapEntry::setFull() {
    num_used_bytes = PAGESIZE;
}


#ifdef CACHE_UTIL
void PageMapEntry::mallocUpdateCacheLines(uint8_t range, uint64_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, unsigned int size) {
    int64_t size_remain = size;
    PageMapEntry * targetPage = ShadowMemory::getPageMapEntry(range, page_index);
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
#endif

void PageMapEntry::freeUpdateCacheLines(uint8_t range, uint64_t page_index, uint8_t cache_index, uint8_t firstCacheLineOffset, ObjStat objStat) {
    int64_t size_remain = objStat.size;
    PageMapEntry * targetPage = ShadowMemory::getPageMapEntry(range, page_index);
    CacheMapEntry * current = targetPage->getCacheMapEntry(true) + cache_index;

    uint8_t curCacheLineBytes = MIN(CACHELINE_SIZE - firstCacheLineOffset, size_remain);  /// The first cache line

#ifdef CACHE_UTIL
    current->subUsedBytes(curCacheLineBytes);
#endif

    /// check conflict
    if(current->misses & (uint8_t)0x80) {
        conflictData.checkObj(objStat.tid, current, objStat.callKey);
//        current->addedConflict = false;
    }
    /// check coherency
    if(current->misses & (uint8_t)0x40) {
        uint8_t i;
        for(i = 0; i < numOfCoherencyCaches && coherencyCaches[i].cme != current; ++i) {} /// find index
        CoherencyData * status = &(coherencyCaches[i]);
//        fprintf(stderr, "%u %u %u %u\n", i, status->allocateTid[0], status->allocateTid[1], objStat.tid);
        if(status->allocateTid[0]) {
            if(status->allocateTid[0] != objStat.tid && !status->allocateTid[1]) {
                status->allocateTid[1] = objStat.tid;
            }
            if(status->callKey[0] != objStat.callKey && !status->callKey[1]) {
                status->callKey[1] = objStat.callKey;
            }
        } else {
            status->allocateTid[0] = objStat.tid;
            status->callKey[0] = objStat.callKey;
        }
//        current->addedCoherency = false;
    }

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

        /// check conflict
        if(current->misses & (uint8_t)0x80) {
            conflictData.checkObj(objStat.tid, current, objStat.callKey);
            current->misses &= (uint8_t)0x7f;
        }

#ifdef CACHE_UTIL
        current->setEmpty();
#endif
        size_remain -= CACHELINE_SIZE;
        cache_index++;

    }
    if(size_remain > 0) { /// the last cache line
        if(cache_index == NUM_CACHELINES_PER_PAGE) {
            cache_index = 0;
            targetPage++;
            current = targetPage->getCacheMapEntry(true);
        } else {
            current++;
        }

#ifdef CACHE_UTIL
        current->subUsedBytes(size_remain);
#endif

        /// check conflict
        if(current->misses & (uint8_t)0x80) {
            conflictData.checkObj(objStat.tid, current, objStat.callKey);
//            current->addedConflict = false;
        }
        /// check coherency
        if(current->misses & (uint8_t)0x40) {
            uint8_t i;
            for(i = 0; i < numOfCoherencyCaches && coherencyCaches[i].cme != current; ++i) {} /// find index
            CoherencyData * status = &(coherencyCaches[i]);
//            fprintf(stderr, "%u %u %u %u\n", i, status->allocateTid[0], status->allocateTid[1], objStat.tid);
            if(status->allocateTid[0]) {
                if(status->allocateTid[0] != objStat.tid && !status->allocateTid[1]) {
                    status->allocateTid[1] = objStat.tid;
                }
                if(status->callKey[0] != objStat.callKey && !status->callKey[1]) {
                    status->callKey[1] = objStat.callKey;
                }
            } else {
                status->allocateTid[0] = objStat.tid;
                status->callKey[0] = objStat.callKey;
            }
        }
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

//#endif