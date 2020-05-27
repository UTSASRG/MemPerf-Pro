#if !defined(__RECORD_SCALE_HH__)
#define __RECORD_SCALE_HH__

/*
 * @file   recordscale.h
 * @brief  record some issues related to the scalability
 * @author Tongping Liu
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sched.h>
#include <string.h>
#include "programstatus.h"
#include "definevalues.h"
#include "libmallocprof.h"
#include "hashmap.hh"
#include "memwaste.h"
#include "allocatingstatus.h"

#define MAX_THREAD_NUMBER 1024
int threadcontention_index;
ThreadContention all_threadcontention_array[MAX_THREAD_NUMBER];
extern thread_local thread_alloc_data localTAD;
thread_local ThreadContention* threadContention;

typedef enum {
    MUTEX,
    SPIN,
    MUTEXTRY,
    SPINTRY,
    NUM_OF_LOCKTYPES
} LockTypes;

struct DetailLockData {
    LockTypes     lockType;  // What is the lock type
    unsigned int numOfCalls[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];        // How many invocations
    unsigned int numOfCallsWithContentions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA]; // How many of them have the contention
    unsigned int numOfContentingThreads;    // How many are waiting
    unsigned int maxNumOfContentingThreads; // How many threads are contending on this lock
    uint64_t cycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA]; // Total cycles

    DetailLockData newDetailLockData(LockTypes lockType) {
        return DetailLockData{lockType, {0}, {0}, 0, 0, 0};
    }

    bool aContentionHappening() {
        return (++numOfContentingThreads >= 2);
    }

    void checkAndUpdateMaxNumOfContentingThreads() {
        maxNumOfContentingThreads = MAX(numOfContentingThreads, maxNumOfContentingThreads);
    }

    void quitFromContenting() {
        numOfContentingThreads--;
    }
};

extern HashMap <uint64_t, LockData, spinlock, PrivateHeap> lockUsage;

auto lockFunctions[NUM_OF_LOCKTYPES] = {
        RealX::pthread_mutex_lock,
        RealX::pthread_spin_lock,
        RealX::pthread_mutex_trylock,
        RealX::pthread_spin_trylock
};

auto unlockFunctions [NUM_OF_LOCKTYPES] = {
        RealX::pthread_mutex_unlock,
        RealX::pthread_spin_unlock,
        RealX::pthread_mutex_unlock,
        RealX::pthread_spin_unlock
};

typedef enum {
    MMAP,
    MADVISE,
    SBRK,
    MPROTECT,
    MUNMAP,
    MREMAP,
    NUM_OF_SYSTEMCALLTYPES
} SystemCallTypes;

struct SystemCallData {
    uint64_t num;
    uint64_t cycles;

    void add(SystemCallData newSystemCallData) {
        this->num += newSystemCallData->num;
        this->cycles += newSystemCallData.cycles;
    }
};


extern "C" {

/* ************************Synchronization******************************** */
#define PTHREAD_LOCK_HANDLE(LOCKTYPE, LOCK) \
  if (AllocatingStatus::outsideTrackedAllocation()) { \
    return lockfuncs[LOCK_TYPE].realLock((void *)LOCK); \
  } \
  \
  DetailLockData * detailLockData = lockUsage.find((void *)LOCK, sizeof(void *)); \
  if(detailLockData == nullptr)  { \
    detailLockData = lockUsage.insert((uint64_t)LOCK, sizeof(uint64_t), DetailLockData.newDetailLockData(LOCKTYPE)); \
    AllocatingStatus::recordANewLock(LOCKTYPE); \
  } \
  \
  AllocatingStatus::initForWritingOneLockData(LOCKTYPE, detailLockData); \
  if(detailLockData.aContentionHappening()) { \
    detailLockData.checkAndUpdateMaxNumOfContentingThreads(); \
    AllocatingStatus::recordALockContention(); \
  } \
  \
  uint64_t timeStart = rdtscp();\
  int result = lockfuncs[LOCK_TYPE].realLock((void *)LOCK); \
  uint64_t timeStop = rdtscp(); \
  \
  AllocatingStatus::recordLockCallAndCycles(1, timeStop-timeStart); \
  AllocatingStatus::checkAndStartRecordingACriticalSection(); \
  return result;


int pthread_mutex_lock(pthread_mutex_t *mutex) {
    PTHREAD_LOCK_HANDLE(LOCK_TYPE_MUTEX, mutex);
}

int pthread_spin_lock(pthread_spinlock_t *lock) {
    PTHREAD_LOCK_HANDLE(LOCK_TYPE_SPINLOCK, lock);
}

int pthread_spin_trylock(pthread_spinlock_t *lock) {
    PTHREAD_LOCK_HANDLE(LOCK_TYPE_SPIN_TRYLOCK, lock);
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    PTHREAD_LOCK_HANDLE(LOCK_TYPE_TRYLOCK, mutex);
}


#define PTHREAD_UNLOCK_HANDLE(LOCK_TYPE, lock) \
    if(AllocatingStatus::outsideTrackedAllocation()) { \
        return unlockfuncs[LOCK_TYPE].realUnlock((void *)lock);
    } \
    DetailLockData * detailLockData = lockUsage.find((void *)lock, sizeof(void *)); \
    detailLockData.quitFromContenting(); \
    AllocatingStatus::checkAndStopRecordingACriticalSection(); \
    int result = unlockfuncs[LOCK_TYPE].realUnlock((void *)lock); \
    return result;

// PTHREAD_MUTEX_UNLOCK
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  PTHREAD_UNLOCK_HANDLE(LOCK_TYPE_MUTEX, mutex);
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {
  PTHREAD_UNLOCK_HANDLE(LOCK_TYPE_SPINLOCK, lock);
}

/* ************************Synchronization End******************************** */

/* ************************Systemn Calls******************************** */


// MMAP
void * yymmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (!realInitialized) RealX::initializer();
    if(AllocatingStatus::outsideTrackedAllocation()) {
        return RealX::mmap(addr, length, prot, flags, fd, offset);
    }

    uint64_t timeStart = rdtscp();
    void* retval = RealX::mmap(addr, length, prot, flags, fd, offset);
    uint64_t timeStop = rdtscp();

    AllocatingStatus::addToSystemCallData(MMAP, SystemCallData{1, timeStop - timeStart});

  return retval;
}

// MADVISE
int madvise(void *addr, size_t length, int advice){
    if (!realInitialized) RealX::initializer();
    if(AllocatingStatus::outsideTrackedAllocation()) {
        return RealX::madvise(addr, length, advice);
    }

    if (advice == MADV_DONTNEED) {
        uint cleanedPageSize = ShadowMemory::cleanupPages((uintptr_t)addr, length);
        MemoryUsage::subTotalSizeFromMemoryUsage(cleanedPageSize);
    }

    uint64_t timeStart = rdtscp();
    int result = RealX::madvise(addr, length, advice);
    uint64_t timeStop = rdtscp();

    AllocatingStatus::addToSystemCallData(MADVISE, SystemCallData{1, timeStop - timeStart});

  return result;
}

// SBRK
void *sbrk(intptr_t increment){
    if (!realInitialized) RealX::initializer();
    if(AllocatingStatus::outsideTrackedAllocation()) {
        return RealX::sbrk(increment);
    }

  uint64_t timeStart = rdtscp();
  void *retptr = RealX::sbrk(increment);
  uint64_t timeStop = rdtscp();

    AllocatingStatus::addToSystemCallData(SBRK, SystemCallData{1, timeStop - timeStart});

  return retptr;
}


// MPROTECT
int mprotect(void* addr, size_t len, int prot) {
  if (!realInitialized) RealX::initializer();
  if(AllocatingStatus::outsideTrackedAllocation()) {
    return RealX::mprotect(addr, len, prot);
  }

  uint64_t timeStart = rdtscp();
  int ret =  RealX::mprotect(addr, len, prot);
  uint64_t timeStop = rdtscp();

    AllocatingStatus::addToSystemCallData(MPROTECT, SystemCallData{1, timeStop - timeStart});

  return ret;
}


int munmap(void *addr, size_t length) {

  if (!realInitialized) RealX::initializer();

  if(AllocatingStatus::outsideTrackedAllocation()) {
      return RealX::munmap(addr, length);
  }

  size_t cleanedPageSize = ShadowMemory::cleanupPages((intptr_t)addr, length);
    MemoryUsage::subTotalSizeFromMemoryUsage(cleanedPageSize);

  uint64_t timeStart = rdtscp();
  int ret =  RealX::munmap(addr, length);
  uint64_t timeStop = rdtscp();

    AllocatingStatus::addToSystemCallData(MUNMAP, SystemCallData{1, timeStop - timeStart});


  return ret;
}


void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ... /*  void *new_address */) {

  if (!realInitialized) RealX::initializer();

    va_list ap;
    va_start(ap, flags);
    void* new_address = va_arg(ap, void*);
    va_end(ap);

    if(AllocatingStatus::outsideTrackedAllocation()) {
    return RealX::mremap(old_address, old_size, new_size, flags, new_address);
  }

  uint64_t timeStart = rdtscp();
  void* ret =  RealX::mremap(old_address, old_size, new_size, flags, new_address);
  uint64_t timeStop = rdtscp();

    AllocatingStatus::addToSystemCallData(MREMAP, SystemCallData{1, timeStop - timeStart});

  return ret;
}



/* ************************Systemn Calls End******************************** */
};
void writeThreadContention() {
    fprintf(stderr, "writing\n");
    fprintf (ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>>>>>>>>>>>> THREAD CONTENTIONS <<<<<<<<<<<<<<<<<<<<<<<<<<\n");

    long maxRealMemoryUsage = 0;
    long maxRealAllocatedMemoryUsage = 0;
    long maxTotalMemoryUsage = 0;

    ThreadContention globalizedThreadContention;

    for(int i = 0; i <= threadcontention_index; i++) {
        ThreadContention* data = &all_threadcontention_array[i];

//        globalizedThreadContention.tid += data->tid;

        globalizedThreadContention.mmap_waits_alloc += data->mmap_waits_alloc;
        globalizedThreadContention.mmap_waits_alloc_large += data->mmap_waits_alloc_large;
        globalizedThreadContention.mmap_waits_free += data->mmap_waits_free;
        globalizedThreadContention.mmap_waits_free_large += data->mmap_waits_free_large;

        globalizedThreadContention.mmap_wait_cycles_alloc += data->mmap_wait_cycles_alloc;
        globalizedThreadContention.mmap_wait_cycles_alloc_large += data->mmap_wait_cycles_alloc_large;
        globalizedThreadContention.mmap_wait_cycles_free += data->mmap_wait_cycles_free;
        globalizedThreadContention.mmap_wait_cycles_free_large += data->mmap_wait_cycles_free_large;

        globalizedThreadContention.sbrk_waits_alloc += data->sbrk_waits_alloc;
        globalizedThreadContention.sbrk_waits_alloc_large += data->sbrk_waits_alloc_large;
        globalizedThreadContention.sbrk_waits_free += data->sbrk_waits_free;
        globalizedThreadContention.sbrk_waits_free_large += data->sbrk_waits_free_large;

        globalizedThreadContention.sbrk_wait_cycles_alloc += data->sbrk_wait_cycles_alloc;
        globalizedThreadContention.sbrk_wait_cycles_alloc_large += data->sbrk_wait_cycles_alloc_large;
        globalizedThreadContention.sbrk_wait_cycles_free += data->sbrk_wait_cycles_free;
        globalizedThreadContention.sbrk_wait_cycles_free_large += data->sbrk_wait_cycles_free_large;

        globalizedThreadContention.madvise_waits_alloc += data->madvise_waits_alloc;
        globalizedThreadContention.madvise_waits_alloc_large += data->madvise_waits_alloc_large;
        globalizedThreadContention.madvise_waits_free += data->madvise_waits_free;
        globalizedThreadContention.madvise_waits_free_large += data->madvise_waits_free_large;

        globalizedThreadContention.madvise_wait_cycles_alloc += data->madvise_wait_cycles_alloc;
        globalizedThreadContention.madvise_wait_cycles_alloc_large += data->madvise_wait_cycles_alloc_large;
        globalizedThreadContention.madvise_wait_cycles_free += data->madvise_wait_cycles_free;
        globalizedThreadContention.madvise_wait_cycles_free_large += data->madvise_wait_cycles_free_large;

        globalizedThreadContention.munmap_waits_alloc += data->munmap_waits_alloc;
        globalizedThreadContention.munmap_waits_alloc_large += data->munmap_waits_alloc_large;
        globalizedThreadContention.munmap_waits_free += data->munmap_waits_free;
        globalizedThreadContention.munmap_waits_free_large += data->munmap_waits_free_large;

        globalizedThreadContention.munmap_wait_cycles_alloc += data->munmap_wait_cycles_alloc;
        globalizedThreadContention.munmap_wait_cycles_alloc_large += data->munmap_wait_cycles_alloc_large;
        globalizedThreadContention.munmap_wait_cycles_free += data->munmap_wait_cycles_free;
        globalizedThreadContention.munmap_wait_cycles_free_large += data->munmap_wait_cycles_free_large;

        globalizedThreadContention.mremap_waits_alloc += data->mremap_waits_alloc;
        globalizedThreadContention.mremap_waits_alloc_large += data->mremap_waits_alloc_large;
        globalizedThreadContention.mremap_waits_free += data->mremap_waits_free;
        globalizedThreadContention.mremap_waits_free_large += data->mremap_waits_free_large;

        globalizedThreadContention.mremap_wait_cycles_alloc += data->mremap_wait_cycles_alloc;
        globalizedThreadContention.mremap_wait_cycles_alloc_large += data->mremap_wait_cycles_alloc_large;
        globalizedThreadContention.mremap_wait_cycles_free += data->mremap_wait_cycles_free;
        globalizedThreadContention.mremap_wait_cycles_free_large += data->mremap_wait_cycles_free_large;

        globalizedThreadContention.mprotect_waits_alloc += data->mprotect_waits_alloc;
        globalizedThreadContention.mprotect_waits_alloc_large += data->mprotect_waits_alloc_large;
        globalizedThreadContention.mprotect_waits_free += data->mprotect_waits_free;
        globalizedThreadContention.mprotect_waits_free_large += data->mprotect_waits_free_large;

        globalizedThreadContention.mprotect_wait_cycles_alloc += data->mprotect_wait_cycles_alloc;
        globalizedThreadContention.mprotect_wait_cycles_alloc_large += data->mprotect_wait_cycles_alloc_large;
        globalizedThreadContention.mprotect_wait_cycles_free += data->mprotect_wait_cycles_free;
        globalizedThreadContention.mprotect_wait_cycles_free_large += data->mprotect_wait_cycles_free_large;

        globalizedThreadContention.maxRealMemoryUsage = MAX(data->maxRealMemoryUsage, globalizedThreadContention.maxRealMemoryUsage);
        globalizedThreadContention.maxRealAllocatedMemoryUsage = MAX(data->maxRealAllocatedMemoryUsage, globalizedThreadContention.maxRealAllocatedMemoryUsage);
        globalizedThreadContention.maxTotalMemoryUsage = MAX(data->maxTotalMemoryUsage, globalizedThreadContention.maxTotalMemoryUsage);
    }

		maxRealMemoryUsage = globalizedThreadContention.maxRealMemoryUsage;
		maxRealAllocatedMemoryUsage = globalizedThreadContention.maxRealAllocatedMemoryUsage;
		maxTotalMemoryUsage = globalizedThreadContention.maxTotalMemoryUsage;

    fprintf (ProgramStatus::outputFile, ">>> small_alloc_mmap\t\t\t\t%20lu\n", globalizedThreadContention.mmap_waits_alloc);
    fprintf (ProgramStatus::outputFile, ">>> small_alloc_mmap_wait_cycles%16lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.mmap_wait_cycles_alloc,
             globalizedThreadContention.mmap_wait_cycles_alloc / (total_cycles/100),
						((double)globalizedThreadContention.mmap_wait_cycles_alloc / safeDivisor(globalizedThreadContention.mmap_waits_alloc)));
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_mmap\t\t\t\t%20lu\n", globalizedThreadContention.mmap_waits_alloc_large);
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_mmap_wait_cycles%16lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.mmap_wait_cycles_alloc_large,
             globalizedThreadContention.mmap_wait_cycles_alloc_large / (total_cycles/100),
             ((double)globalizedThreadContention.mmap_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.mmap_waits_alloc_large)));
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_mmap\t\t\t%20lu\n", globalizedThreadContention.mmap_waits_free);
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_mmap_wait_cycles%14lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.mmap_wait_cycles_free,
             globalizedThreadContention.mmap_wait_cycles_free / (total_cycles/100),
            ((double)globalizedThreadContention.mmap_wait_cycles_free / safeDivisor(globalizedThreadContention.mmap_waits_free)));
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_mmap\t\t\t%20lu\n", globalizedThreadContention.mmap_waits_free_large);
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_mmap_wait_cycles%14lu(%3d%%)\tavg = %.1f\n\n",
             globalizedThreadContention.mmap_wait_cycles_free_large,
             globalizedThreadContention.mmap_wait_cycles_free_large / (total_cycles/100),
            ((double)globalizedThreadContention.mmap_wait_cycles_free_large / safeDivisor(globalizedThreadContention.mmap_waits_free_large)));


    fprintf (ProgramStatus::outputFile, ">>> small_alloc_sbrk\t\t\t\t%20lu\n", globalizedThreadContention.sbrk_waits_alloc);
    fprintf (ProgramStatus::outputFile, ">>> small_alloc_sbrk_wait_cycles%16lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.sbrk_wait_cycles_alloc,
             globalizedThreadContention.sbrk_wait_cycles_alloc / (total_cycles/100),
             ((double)globalizedThreadContention.sbrk_wait_cycles_alloc / safeDivisor(globalizedThreadContention.sbrk_waits_alloc)));
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_sbrk\t\t\t\t%20lu\n", globalizedThreadContention.sbrk_waits_alloc_large);
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_sbrk_wait_cycles%16lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.sbrk_wait_cycles_alloc_large,
             globalizedThreadContention.sbrk_wait_cycles_alloc_large / (total_cycles/100),
            ((double)globalizedThreadContention.sbrk_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.sbrk_waits_alloc_large)));
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_sbrk\t\t\t%20lu\n", globalizedThreadContention.sbrk_waits_free);
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_sbrk_wait_cycles%14lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.sbrk_wait_cycles_free,
             globalizedThreadContention.sbrk_wait_cycles_free / (total_cycles/100),
            ((double)globalizedThreadContention.sbrk_wait_cycles_free / safeDivisor(globalizedThreadContention.sbrk_waits_free)));
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_sbrk\t\t\t%20lu\n", globalizedThreadContention.sbrk_waits_free_large);
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_sbrk_wait_cycles%14lu(%3d%%)\tavg = %.1f\n\n",
             globalizedThreadContention.sbrk_wait_cycles_free_large,
             globalizedThreadContention.sbrk_wait_cycles_free_large / (total_cycles/100),
            ((double)globalizedThreadContention.sbrk_wait_cycles_free_large / safeDivisor(globalizedThreadContention.sbrk_waits_free_large)));

    fprintf (ProgramStatus::outputFile, ">>> small_alloc_madvise\t\t\t%20lu\n", globalizedThreadContention.madvise_waits_alloc);
    fprintf (ProgramStatus::outputFile, ">>> small_alloc_madvise_wait_cycles%13lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.madvise_wait_cycles_alloc,
             globalizedThreadContention.madvise_wait_cycles_alloc / (total_cycles/100),
            ((double)globalizedThreadContention.madvise_wait_cycles_alloc / safeDivisor(globalizedThreadContention.madvise_waits_alloc)));
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_madvise\t\t\t%20lu\n", globalizedThreadContention.madvise_waits_alloc_large);
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_madvise_wait_cycles%13lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.madvise_wait_cycles_alloc_large,
             globalizedThreadContention.madvise_wait_cycles_alloc_large / (total_cycles/100),
            ((double)globalizedThreadContention.madvise_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.madvise_waits_alloc_large)));
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_madvise\t\t%20lu\n", globalizedThreadContention.madvise_waits_free);
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_madvise_wait_cycles%11lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.madvise_wait_cycles_free,
             globalizedThreadContention.madvise_wait_cycles_free / (total_cycles/100),
            ((double)globalizedThreadContention.madvise_wait_cycles_free / safeDivisor(globalizedThreadContention.madvise_waits_free)));
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_madvise\t\t%20lu\n", globalizedThreadContention.madvise_waits_free_large);
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_madvise_wait_cycles%11lu(%3d%%)\tavg = %.1f\n\n",
             globalizedThreadContention.madvise_wait_cycles_free_large,
             globalizedThreadContention.madvise_wait_cycles_free_large / (total_cycles/100),
            ((double)globalizedThreadContention.madvise_wait_cycles_free_large / safeDivisor(globalizedThreadContention.madvise_waits_free_large)));

    fprintf (ProgramStatus::outputFile, ">>> small_alloc_munmap\t\t\t%20lu\n", globalizedThreadContention.munmap_waits_alloc);
    fprintf (ProgramStatus::outputFile, ">>> small_alloc_munmap_wait_cycles%14lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.munmap_wait_cycles_alloc,
             globalizedThreadContention.munmap_wait_cycles_alloc / (total_cycles/100),
            ((double)globalizedThreadContention.munmap_wait_cycles_alloc / safeDivisor(globalizedThreadContention.munmap_waits_alloc)));
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_munmap\t\t\t%20lu\n", globalizedThreadContention.munmap_waits_alloc_large);
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_munmap_wait_cycles%14lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.munmap_wait_cycles_alloc_large,
             globalizedThreadContention.munmap_wait_cycles_alloc_large / (total_cycles/100),
            ((double)globalizedThreadContention.munmap_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.munmap_waits_alloc_large)));
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_munmap\t\t%20lu\n", globalizedThreadContention.munmap_waits_free);
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_munmap_wait_cycles%12lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.munmap_wait_cycles_free,
             globalizedThreadContention.munmap_wait_cycles_free / (total_cycles/100),
            ((double)globalizedThreadContention.munmap_wait_cycles_free / safeDivisor(globalizedThreadContention.munmap_waits_free)));
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_munmap\t\t%20lu\n", globalizedThreadContention.munmap_waits_free_large);
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_munmap_wait_cycles%12lu(%3d%%)\tavg = %.1f\n\n",
             globalizedThreadContention.munmap_wait_cycles_free_large,
             globalizedThreadContention.munmap_wait_cycles_free_large / (total_cycles/100),
            ((double)globalizedThreadContention.munmap_wait_cycles_free_large / safeDivisor(globalizedThreadContention.munmap_waits_free_large)));

    fprintf (ProgramStatus::outputFile, ">>> small_alloc_mremap\t\t\t%20lu\n", globalizedThreadContention.mremap_waits_alloc);
    fprintf (ProgramStatus::outputFile, ">>> small_alloc_mremap_wait_cycles%14lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.mremap_wait_cycles_alloc,
             globalizedThreadContention.mremap_wait_cycles_alloc / (total_cycles/100),
            ((double)globalizedThreadContention.mremap_wait_cycles_alloc / safeDivisor(globalizedThreadContention.mremap_waits_alloc)));
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_mremap\t\t\t%20lu\n", globalizedThreadContention.mremap_waits_alloc_large);
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_mremap_wait_cycles%14lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.mremap_wait_cycles_alloc_large,
             globalizedThreadContention.mremap_wait_cycles_alloc_large / (total_cycles/100),
            ((double)globalizedThreadContention.mremap_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.mremap_waits_alloc_large)));
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_mremap\t\t%20lu\n", globalizedThreadContention.mremap_waits_free);
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_mremap_wait_cycles%12lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.mremap_wait_cycles_free,
             globalizedThreadContention.mremap_wait_cycles_free / (total_cycles/100),
            ((double)globalizedThreadContention.mremap_wait_cycles_free / safeDivisor(globalizedThreadContention.mremap_waits_free)));
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_mremap\t\t%20lu\n", globalizedThreadContention.mremap_waits_free_large);
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_mremap_wait_cycles%12lu(%3d%%)\tavg = %.1f\n\n",
             globalizedThreadContention.mremap_wait_cycles_free_large,
             globalizedThreadContention.mremap_wait_cycles_free_large / (total_cycles/100),
            ((double)globalizedThreadContention.mremap_wait_cycles_free_large / safeDivisor(globalizedThreadContention.mremap_waits_free_large)));


    fprintf (ProgramStatus::outputFile, ">>> small_alloc_mprotect\t\t%20lu\n", globalizedThreadContention.mprotect_waits_alloc);
    fprintf (ProgramStatus::outputFile, ">>> small_alloc_mprotect_wait_cycles%12lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.mprotect_wait_cycles_alloc,
             globalizedThreadContention.mprotect_wait_cycles_alloc / (total_cycles/100),
            ((double)globalizedThreadContention.mprotect_wait_cycles_alloc / safeDivisor(globalizedThreadContention.mprotect_waits_alloc)));
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_mprotect\t\t%20lu\n", globalizedThreadContention.mprotect_waits_alloc_large);
    fprintf (ProgramStatus::outputFile, ">>> large_alloc_mprotect_wait_cycles%12lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.mprotect_wait_cycles_alloc_large,
             globalizedThreadContention.mprotect_wait_cycles_alloc_large / (total_cycles/100),
            ((double)globalizedThreadContention.mprotect_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.mprotect_waits_alloc_large)));
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_mprotect\t%20lu\n", globalizedThreadContention.mprotect_waits_free);
    fprintf (ProgramStatus::outputFile, ">>> small_dealloc_mprotect_wait_cycles%10lu(%3d%%)\tavg = %.1f\n",
             globalizedThreadContention.mprotect_wait_cycles_free,
             globalizedThreadContention.mprotect_wait_cycles_free / (total_cycles/100),
            ((double)globalizedThreadContention.mprotect_wait_cycles_free / safeDivisor(globalizedThreadContention.mprotect_waits_free)));
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_mprotect\t%20lu\n", globalizedThreadContention.mprotect_waits_free_large);
    fprintf (ProgramStatus::outputFile, ">>> large_dealloc_mprotect_wait_cycles%10lu(%3d%%)\tavg = %.1f\n\n",
             globalizedThreadContention.mprotect_wait_cycles_free_large,
             globalizedThreadContention.mprotect_wait_cycles_free_large / (total_cycles/100),
            ((double)globalizedThreadContention.mprotect_wait_cycles_free_large / safeDivisor(globalizedThreadContention.mprotect_waits_free_large)));


		long realMem = MAX(max_mu.realMemoryUsage, maxRealMemoryUsage);
    long realAllocMem = realMem + MemoryWaste::recordSumup();
		totalMem = MAX(max_mu.totalMemoryUsage, MAX(maxTotalMemoryUsage, realAllocMem));
		fprintf (ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>>>>>>>>>>>> Total Memory Usage <<<<<<<<<<<<<<<<<<<<<<<<<\n");
    fprintf (ProgramStatus::outputFile, ">>> Max Memory Usage in Threads:\n");
    fprintf (ProgramStatus::outputFile, ">>> maxRealMemoryUsage\t\t%20zuK\n", maxRealMemoryUsage/1024);
    fprintf (ProgramStatus::outputFile, ">>> maxRealAllocMemoryUsage%19zuK\n", maxRealAllocatedMemoryUsage/1024);
    fprintf (ProgramStatus::outputFile, ">>> maxTotalMemoryUsage\t\t%20zuK\n\n", maxTotalMemoryUsage/1024);
    fprintf (ProgramStatus::outputFile, ">>> Global Memory Usage:\n");
    fprintf (ProgramStatus::outputFile, ">>> realMemoryUsage\t\t\t\t%20zuK\n", realMem/1024);
    fprintf (ProgramStatus::outputFile, ">>> realAllocatedMemoryUsage%18zuK\n", realAllocMem/1024);
    fprintf (ProgramStatus::outputFile, ">>> totalMemoryUsage\t\t\t%20zuK\n", totalMem/1024);
    MemoryWaste::reportMaxMemory(ProgramStatus::outputFile);
}

#endif
