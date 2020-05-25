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



typedef struct {
  LockType     type;  // What is the lock type 
  unsigned int calls;        // How many invocations
  unsigned int contendCalls; // How many of them have the contention
  int contendThreads;    // How many are waiting
  int maxContendThreads; // How many threads are contending on this lock
  unsigned long cycles; // Total cycles
    unsigned long percalls[4] = {0, 0, 0, 0};
    unsigned long percycles[4] = {0, 0, 0, 0};
} PerLockData;

extern HashMap <uint64_t, PerLockData, spinlock, PrivateHeap> lockUsage;
typedef int (*myLockType) (void *);
typedef int (*myUnlockType) (void *);

typedef struct {
  int (*realLock)(void *); 
} LockFuncs; 

LockFuncs lockfuncs[LOCK_TYPE_TOTAL] = 
  { (myLockType)RealX::pthread_mutex_lock,
    (myLockType)RealX::pthread_spin_lock,
    (myLockType)RealX::pthread_mutex_trylock,
    (myLockType)RealX::pthread_spin_trylock };

typedef struct {
  int (*realUnlock)(void *); 
} UnlockFuncs; 

UnlockFuncs unlockfuncs [LOCK_TYPE_TOTAL] =
  { (myUnlockType)RealX::pthread_mutex_unlock,
    (myUnlockType)RealX::pthread_spin_unlock,
    (myUnlockType)RealX::pthread_mutex_unlock,
    (myUnlockType)RealX::pthread_spin_unlock };


extern "C" {

void setThreadContention() {
  int current_index = __atomic_add_fetch(&threadcontention_index, 1, __ATOMIC_RELAXED);
  if(current_index >= MAX_THREAD_NUMBER) {
    fprintf(stderr, "Please increase thread number: MAX_THREAD_NUMBER, %d\n", MAX_THREAD_NUMBER);
    abort();
  }
  threadContention = &all_threadcontention_array[current_index];
    threadContention->tid = current_index;
    thrData.tid = current_index;

//    cpu_set_t mask;
//    CPU_ZERO(&mask);
//    CPU_SET(thrData.tid%40, &mask);
//    if(sched_setaffinity(0, sizeof(mask), &mask) < 0 ) {
//        fprintf(stderr, "sched_setaffinity %d\n", thrData.tid);
//    }
}


/* ************************Synchronization******************************** */
// In the PTHREAD_LOCK_HANDLE, we will pretend the initialization
#define PTHREAD_LOCK_HANDLE(LOCK_TYPE, LOCK) \
  if(!mapsInitialized) { \
    return 0; \
  } \
  if (!realing || !inRealMain) { \
    return lockfuncs[LOCK_TYPE].realLock((void *)LOCK); \
  } \
  PerPrimitiveData *pmdata = &threadContention->pmdata[LOCK_TYPE]; \
  PerLockData * thisLock = lockUsage.find((uint64_t)LOCK, sizeof(uint64_t)); \
  if(thisLock == NULL)  { \
    PerLockData  newLock; \
    memset(&newLock, 0, sizeof(PerLockData)); \
    newLock.type = LOCK_TYPE;  \
    thisLock = lockUsage.insert((uint64_t)LOCK, sizeof(uint64_t), newLock); \
    localTAD.lock_nums[LOCK_TYPE]++; \
  } \
  int contendThreads = ++(thisLock->contendThreads); \
  if(contendThreads > 1) { \
    thisLock->contendCalls ++; \
  } \
  if(contendThreads > thisLock->maxContendThreads){ \
    thisLock->maxContendThreads = contendThreads; \
  }\
  uint64_t timeStart = rdtscp();\
  int result = lockfuncs[LOCK_TYPE].realLock((void *)LOCK); \
  uint64_t timeStop = rdtscp(); \
  uint64_t timediff = 0; \
  if(timeStop > timeStart) { \
    timediff = timeStop - timeStart; \
} \
    if(!inFree) {\
if(now_size < large_object_threshold) {\
pmdata->calls[0]++; \
pmdata->cycles[0] += (timeStop - timeStart); \
  thisLock->percalls[0]++;  \
  thisLock->percycles[0] += (timeStop - timeStart); \
} else { \
pmdata->calls[1]++; \
pmdata->cycles[1] += (timeStop - timeStart); \
  thisLock->percalls[1]++;  \
  thisLock->percycles[1] += (timeStop - timeStart); \
} \
} else { \
if(now_size < large_object_threshold) { \
pmdata->calls[2]++; \
pmdata->cycles[2] += (timeStop - timeStart); \
  thisLock->percalls[2]++;  \
  thisLock->percycles[2] += (timeStop - timeStart); \
} else { \
pmdata->calls[3]++; \
pmdata->cycles[3] += (timeStop - timeStart); \
  thisLock->percalls[3]++;  \
  thisLock->percycles[3] += (timeStop - timeStart); \
} \
} \
  thisLock->calls++;  \
  thisLock->cycles += (timeStop - timeStart); \
  if(threadContention->lock_counter == 0) { \
    threadContention->critical_section_start = timeStop; \
  } \
  threadContention->lock_counter++; \
  return result;

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    PTHREAD_LOCK_HANDLE(LOCK_TYPE_MUTEX, mutex);
}
// PTHREAD_SPIN_LOCK
int pthread_spin_lock(pthread_spinlock_t *lock) {
    PTHREAD_LOCK_HANDLE(LOCK_TYPE_SPINLOCK, lock);
}

// PTHREAD_SPIN_TRYLOCK
int pthread_spin_trylock(pthread_spinlock_t *lock) {
    PTHREAD_LOCK_HANDLE(LOCK_TYPE_SPIN_TRYLOCK, lock);
}

// PTHREAD_MUTEX_TRYLOCK
int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    PTHREAD_LOCK_HANDLE(LOCK_TYPE_TRYLOCK, mutex);
}

#define PTHREAD_UNLOCK_HANDLE(LOCK_TYPE, lock) \
  if(!mapsInitialized) { \
    return 0; \
  } \
  if (realing && inRealMain) { \
    PerLockData * thisLock = lockUsage.find((uint64_t)lock, sizeof(uint64_t)); \
    thisLock->contendThreads--; \
    threadContention->lock_counter--; \
    if(threadContention->lock_counter == 0) { \
      uint64_t duration = rdtscp() - threadContention->critical_section_start; \
      threadContention->critical_section_duration += duration; \
      threadContention->critical_section_counter++; \
    } \
    int result = unlockfuncs[LOCK_TYPE].realUnlock((void *)lock); \
    return result; \
  } \
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
        uint returned = PAGESIZE * ShadowMemory::cleanupPages((uintptr_t)addr, length);
        if(threadContention->totalMemoryUsage > returned) {
            threadContention->totalMemoryUsage -= returned;
        }
        __atomic_sub_fetch(&mu.totalMemoryUsage, returned, __ATOMIC_RELAXED);
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

  // It is extremely important that the call to cleanupPages() occurs BEFORE the call
  // to RealX::unmap(). If the orders were switched, it would then be possible for
  // another thread to reallocate the unmapped region *while* the current thread is
  // still performing the cleanupPages() call, thus clearing the other thread's pages.
  ulong returned = PAGESIZE * ShadowMemory::cleanupPages((intptr_t)addr, length);
  if(threadContention->totalMemoryUsage > returned) {
      threadContention->totalMemoryUsage -= returned;
  }
    __atomic_sub_fetch(&mu.totalMemoryUsage, returned, __ATOMIC_RELAXED);

  uint64_t timeStart = rdtscp();
  int ret =  RealX::munmap(addr, length);
  uint64_t timeStop = rdtscp();

    AllocatingStatus::addToSystemCallData(MUNMAP, SystemCallData{1, timeStop - timeStart});


  return ret;
}


void *mremap(void *old_address, size_t old_size, size_t new_size,
    int flags, ... /*  void *new_address */) {
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


extern uint64_t total_cycles;
long totalMem;

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
