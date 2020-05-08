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


#define MAX_THREAD_NUMBER 1024
int threadcontention_index = -1;
ThreadContention all_threadcontention_array[MAX_THREAD_NUMBER];
extern thread_local thread_alloc_data localTAD;
//extern uint num_sbrk;
//extern uint num_madvise;
//extern uint malloc_mmaps;
//extern size_t size_sbrk;
thread_local ThreadContention* threadContention;

extern thread_local bool inAllocation;
extern thread_local bool inFree;
extern thread_local size_t now_size;
extern thread_local bool inMmap;
extern bool realInitialized;
extern bool mapsInitialized;
extern bool selfmapInitialized;
extern initStatus profilerInitialized;

extern MemoryUsage max_mu;

typedef struct {
  LockType     type;  // What is the lock type 
  unsigned int calls;        // How many invocations
  unsigned int contendCalls; // How many of them have the contention
  int contendThreads;    // How many are waiting
  int maxContendThreads; // How many threads are contending on this lock
  unsigned long cycles; // Total cycles
} PerLockData;


//extern HashMap <uint64_t, MmapTuple*, spinlock> mappings;
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

//void checkGlobalMemoryUsageBySizes();
void checkGlobalRealMemoryUsage();
void checkGlobalAllocatedMemoryUsage();
void checkGlobalTotalMemoryUsage();
void checkGlobalMemoryUsage();

extern "C" {

void setThreadContention() {
  int current_index = __atomic_add_fetch(&threadcontention_index, 1, __ATOMIC_RELAXED);
  if(current_index >= MAX_THREAD_NUMBER) {
    fprintf(stderr, "Please increase thread number: MAX_THREAD_NUMBER, %d\n", MAX_THREAD_NUMBER);
    abort();
  }
  threadContention = &all_threadcontention_array[current_index];
  threadContention->tid = gettid();
}

extern void countEventsOutside(bool end);

/* ************************Synchronization******************************** */
// In the PTHREAD_LOCK_HANDLE, we will pretend the initialization
#define PTHREAD_LOCK_HANDLE(LOCK_TYPE, LOCK) \
  if(!mapsInitialized) { \
    return 0; \
  } \
  if (!inAllocation) { \
    return lockfuncs[LOCK_TYPE].realLock((void *)LOCK); \
  } \
  PerPrimitiveData *pmdata = &threadContention->pmdata[LOCK_TYPE]; \
  pmdata->calls++; \
  PerLockData * thisLock = lockUsage.find((uint64_t)LOCK, sizeof(uint64_t)); \
  if(thisLock == NULL)  { \
    PerLockData  newLock; \
    memset(&newLock, 0, sizeof(PerLockData)); \
    newLock.type = LOCK_TYPE;  \
    thisLock = lockUsage.insert((uint64_t)LOCK, sizeof(uint64_t), newLock); \
    localTAD.lock_nums[LOCK_TYPE]++; \
  } \
  thisLock->calls++;  \
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
  pmdata->cycles += (timeStop - timeStart); \
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
  if (inAllocation) { \
    PerLockData * thisLock = lockUsage.find((uint64_t)lock, sizeof(uint64_t)); \
    thisLock->contendThreads--; \
    threadContention->lock_counter--; \
    if(threadContention->lock_counter == 0) { \
      uint64_t duration = rdtscp() - threadContention->critical_section_start; \
      threadContention->critical_section_duration += duration; \
      threadContention->critical_section_counter++; \
    } \
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


// MMAP
void * yymmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {

  if (!realInitialized) {
      RealX::initializer();
  }

  if (!mapsInitialized) {
      return RealX::mmap (addr, length, prot, flags, fd, offset);
  }

  if (inMmap) {
      return RealX::mmap (addr, length, prot, flags, fd, offset);
  }

    if(!inAllocation) {
        return RealX::mmap (addr, length, prot, flags, fd, offset);
    }
    //thread_local
    inMmap = true;

    uint64_t timeStart = rdtscp();
    void* retval = RealX::mmap(addr, length, prot, flags, fd, offset);
    uint64_t timeStop = rdtscp();

    uint64_t address = (uint64_t)retval;

  //If this thread currently doing an allocation
    if(!inFree) {
        if(now_size < large_object_threshold) {
            threadContention->mmap_waits_alloc++;
            threadContention->mmap_wait_cycles_alloc += (timeStop - timeStart);
        } else {
            threadContention->mmap_waits_alloc_large++;
            threadContention->mmap_wait_cycles_alloc_large += (timeStop - timeStart);
        }
    } else {
        if(now_size < large_object_threshold) {
            threadContention->mmap_waits_free++;
            threadContention->mmap_wait_cycles_free += (timeStop - timeStart);
        } else {
            threadContention->mmap_waits_free_large++;
            threadContention->mmap_wait_cycles_free_large += (timeStop - timeStart);
        }
    }

    //mappings.insert(address, newMmapTuple(address, length, prot, 'a'));

	// Need to check if selfmap.getInstance().getTextRegions() has
	// ran. If it hasn't, we can't call isAllocatorInCallStack()

  inMmap = false;
  return retval;
}


// MADVISE
int madvise(void *addr, size_t length, int advice){
  if (!realInitialized) RealX::initializer();

  if (!inAllocation) {
    return RealX::madvise(addr, length, advice);
  }

  if (advice == MADV_DONTNEED) {
    uint returned = PAGESIZE * ShadowMemory::cleanupPages((uintptr_t)addr, length);
    if(threadContention->totalMemoryUsage > returned) {
      threadContention->totalMemoryUsage -= returned;
    }
  }

  uint64_t timeStart = rdtscp();
  int result = RealX::madvise(addr, length, advice);
  uint64_t timeStop = rdtscp();

  if(!inFree) {
      if(now_size < large_object_threshold) {
          threadContention->madvise_waits_alloc++;
          threadContention->madvise_wait_cycles_alloc += (timeStop - timeStart);
      } else {
          threadContention->madvise_waits_alloc_large++;
          threadContention->madvise_wait_cycles_alloc_large += (timeStop - timeStart);
      }
  } else {
      if(now_size < large_object_threshold) {
          threadContention->madvise_waits_free++;
          threadContention->madvise_wait_cycles_free += (timeStop - timeStart);
      } else {
          threadContention->madvise_waits_free_large++;
          threadContention->madvise_wait_cycles_free_large += (timeStop - timeStart);
      }
  }
  return result;
}


// SBRK
void *sbrk(intptr_t increment){
  if (!realInitialized) RealX::initializer();
  if(profilerInitialized != INITIALIZED || !inAllocation)
      return RealX::sbrk(increment);

  uint64_t timeStart = rdtscp();
  void *retptr = RealX::sbrk(increment);
  uint64_t timeStop = rdtscp();

  if(!inFree) {
      if(now_size < large_object_threshold) {
          threadContention->sbrk_waits_alloc++;
          threadContention->sbrk_wait_cycles_alloc += (timeStop - timeStart);
      } else {
          threadContention->sbrk_waits_alloc_large++;
          threadContention->sbrk_wait_cycles_alloc_large += (timeStop - timeStart);
      }
  } else {
      if(now_size < large_object_threshold) {
          threadContention->sbrk_waits_free++;
          threadContention->sbrk_wait_cycles_free += (timeStop - timeStart);
      } else {
          threadContention->sbrk_waits_free_large++;
          threadContention->sbrk_wait_cycles_free_large += (timeStop - timeStart);
      }
  }

  return retptr;
}


// MPROTECT
int mprotect(void* addr, size_t len, int prot) {
  if (!realInitialized) RealX::initializer();

  if(!inAllocation){
    return RealX::mprotect(addr, len, prot);
  }

  uint64_t timeStart = rdtscp();
  int ret =  RealX::mprotect(addr, len, prot);
  uint64_t timeStop = rdtscp();

    if(!inFree) {
        if(now_size < large_object_threshold) {
            threadContention->mprotect_waits_alloc++;
            threadContention->mprotect_wait_cycles_alloc += (timeStop - timeStart);
        } else {
            threadContention->mprotect_waits_alloc_large++;
            threadContention->mprotect_wait_cycles_alloc_large += (timeStop - timeStart);
        }
    } else {
        if(now_size < large_object_threshold) {
            threadContention->mprotect_waits_free++;
            threadContention->mprotect_wait_cycles_free += (timeStop - timeStart);
        } else {
            threadContention->mprotect_waits_free_large++;
            threadContention->mprotect_wait_cycles_free_large += (timeStop - timeStart);
        }
    }

  return ret;
}


int munmap(void *addr, size_t length) {

  if (!realInitialized) RealX::initializer();

  if(!inAllocation){
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

  uint64_t timeStart = rdtscp();
  int ret =  RealX::munmap(addr, length);
  uint64_t timeStop = rdtscp();

  if(!inFree) {
      if(now_size < large_object_threshold) {
          threadContention->munmap_waits_alloc++;
          threadContention->munmap_wait_cycles_alloc += (timeStop - timeStart);
      } else {
          threadContention->munmap_waits_alloc_large++;
          threadContention->munmap_wait_cycles_alloc_large += (timeStop - timeStart);
      }
  } else {
      if(now_size < large_object_threshold) {
          threadContention->munmap_waits_free++;
          threadContention->munmap_wait_cycles_free += (timeStop - timeStart);
      } else {
          threadContention->munmap_waits_free_large++;
          threadContention->munmap_wait_cycles_free_large += (timeStop - timeStart);
      }
  }
  return ret;
}


void *mremap(void *old_address, size_t old_size, size_t new_size,
    int flags, ... /*  void *new_address */) {
  if (!realInitialized) RealX::initializer();

  va_list ap;
  va_start(ap, flags);
  void* new_address = va_arg(ap, void*);
  va_end(ap);

  if(!inAllocation){
    return RealX::mremap(old_address, old_size, new_size, flags, new_address);
  }

  uint64_t timeStart = rdtscp();
  void* ret =  RealX::mremap(old_address, old_size, new_size, flags, new_address);
  uint64_t timeStop = rdtscp();

    if(!inFree) {
        if(now_size < large_object_threshold) {
            threadContention->mremap_waits_alloc++;
            threadContention->mremap_wait_cycles_alloc += (timeStop - timeStart);
        } else {
            threadContention->mremap_waits_alloc_large++;
            threadContention->mremap_wait_cycles_alloc_large += (timeStop - timeStart);
        }
    } else {
        if(now_size < large_object_threshold) {
            threadContention->mremap_waits_free++;
            threadContention->mremap_wait_cycles_free += (timeStop - timeStart);
        } else {
            threadContention->mremap_waits_free_large++;
            threadContention->mremap_wait_cycles_free_large += (timeStop - timeStart);
        }
    }


  return ret;
}

/* ************************Systemn Calls End******************************** */
};

void writeThreadContention() {
    fprintf(stderr, "writing\n");
    fprintf (thrData.output, "\n>>>>>>>>>>>>>>>>>>>>>>>>>> THREAD CONTENTIONS <<<<<<<<<<<<<<<<<<<<<<<<<<\n");

    long maxRealMemoryUsage = 0;
    long maxRealAllocatedMemoryUsage = 0;
    long maxTotalMemoryUsage = 0;

    ThreadContention globalizedThreadContention;

    for(int i = 0; i <= threadcontention_index; i++) {
        ThreadContention* data = &all_threadcontention_array[i];

        globalizedThreadContention.tid += data->tid;
        for(int j = LOCK_TYPE_MUTEX; j < LOCK_TYPE_TOTAL; j++) {
          globalizedThreadContention.pmdata[j].calls += data->pmdata[j].calls;
          globalizedThreadContention.pmdata[j].cycles += data->pmdata[j].cycles;
        }
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

        globalizedThreadContention.critical_section_counter += data->critical_section_counter;
        globalizedThreadContention.critical_section_duration += data->critical_section_duration;

        globalizedThreadContention.maxRealMemoryUsage = MAX(data->maxRealMemoryUsage, globalizedThreadContention.maxRealMemoryUsage);
        globalizedThreadContention.maxRealAllocatedMemoryUsage = MAX(data->maxRealAllocatedMemoryUsage, globalizedThreadContention.maxRealAllocatedMemoryUsage);
        globalizedThreadContention.maxTotalMemoryUsage = MAX(data->maxTotalMemoryUsage, globalizedThreadContention.maxTotalMemoryUsage);
    }

		maxRealMemoryUsage = globalizedThreadContention.maxRealMemoryUsage;
		maxRealAllocatedMemoryUsage = globalizedThreadContention.maxRealAllocatedMemoryUsage;
		maxTotalMemoryUsage = globalizedThreadContention.maxTotalMemoryUsage;

    fprintf (thrData.output, ">>> small_alloc_mmap\t\t\t\t%20lu\n", globalizedThreadContention.mmap_waits_alloc);
    fprintf (thrData.output, ">>> small_alloc_mmap_wait_cycles%16lu\tavg = %.1f\n",
             globalizedThreadContention.mmap_wait_cycles_alloc,
						((double)globalizedThreadContention.mmap_wait_cycles_alloc / safeDivisor(globalizedThreadContention.mmap_waits_alloc)));
    fprintf (thrData.output, ">>> large_alloc_mmap\t\t\t\t%20lu\n", globalizedThreadContention.mmap_waits_alloc_large);
    fprintf (thrData.output, ">>> large_alloc_mmap_wait_cycles%16lu\tavg = %.1f\n",
             globalizedThreadContention.mmap_wait_cycles_alloc_large,
             ((double)globalizedThreadContention.mmap_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.mmap_waits_alloc_large)));
    fprintf (thrData.output, ">>> small_dealloc_mmap\t\t\t%20lu\n", globalizedThreadContention.mmap_waits_free);
    fprintf (thrData.output, ">>> small_dealloc_mmap_wait_cycles%14lu\tavg = %.1f\n",
             globalizedThreadContention.mmap_wait_cycles_free,
             ((double)globalizedThreadContention.mmap_wait_cycles_free / safeDivisor(globalizedThreadContention.mmap_waits_free)));
    fprintf (thrData.output, ">>> large_dealloc_mmap\t\t\t%20lu\n", globalizedThreadContention.mmap_waits_free_large);
    fprintf (thrData.output, ">>> large_dealloc_mmap_wait_cycles%14lu\tavg = %.1f\n\n",
             globalizedThreadContention.mmap_wait_cycles_free_large,
             ((double)globalizedThreadContention.mmap_wait_cycles_free_large / safeDivisor(globalizedThreadContention.mmap_waits_free_large)));


    fprintf (thrData.output, ">>> small_alloc_sbrk\t\t\t\t%20lu\n", globalizedThreadContention.sbrk_waits_alloc);
    fprintf (thrData.output, ">>> small_alloc_sbrk_wait_cycles%16lu\tavg = %.1f\n",
             globalizedThreadContention.sbrk_wait_cycles_alloc,
             ((double)globalizedThreadContention.sbrk_wait_cycles_alloc / safeDivisor(globalizedThreadContention.sbrk_waits_alloc)));
    fprintf (thrData.output, ">>> large_alloc_sbrk\t\t\t\t%20lu\n", globalizedThreadContention.sbrk_waits_alloc_large);
    fprintf (thrData.output, ">>> large_alloc_sbrk_wait_cycles%16lu\tavg = %.1f\n",
             globalizedThreadContention.sbrk_wait_cycles_alloc_large,
             ((double)globalizedThreadContention.sbrk_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.sbrk_waits_alloc_large)));
    fprintf (thrData.output, ">>> small_dealloc_sbrk\t\t\t%20lu\n", globalizedThreadContention.sbrk_waits_free);
    fprintf (thrData.output, ">>> small_dealloc_sbrk_wait_cycles%14lu\tavg = %.1f\n",
             globalizedThreadContention.sbrk_wait_cycles_free,
             ((double)globalizedThreadContention.sbrk_wait_cycles_free / safeDivisor(globalizedThreadContention.sbrk_waits_free)));
    fprintf (thrData.output, ">>> large_dealloc_sbrk\t\t\t%20lu\n", globalizedThreadContention.sbrk_waits_free_large);
    fprintf (thrData.output, ">>> large_dealloc_sbrk_wait_cycles%14lu\tavg = %.1f\n\n",
             globalizedThreadContention.sbrk_wait_cycles_free_large,
             ((double)globalizedThreadContention.sbrk_wait_cycles_free_large / safeDivisor(globalizedThreadContention.sbrk_waits_free_large)));

    fprintf (thrData.output, ">>> small_alloc_madvise\t\t\t%20lu\n", globalizedThreadContention.madvise_waits_alloc);
    fprintf (thrData.output, ">>> small_alloc_madvise_wait_cycles%13lu\tavg = %.1f\n",
             globalizedThreadContention.madvise_wait_cycles_alloc,
             ((double)globalizedThreadContention.madvise_wait_cycles_alloc / safeDivisor(globalizedThreadContention.madvise_waits_alloc)));
    fprintf (thrData.output, ">>> large_alloc_madvise\t\t\t%20lu\n", globalizedThreadContention.madvise_waits_alloc_large);
    fprintf (thrData.output, ">>> large_alloc_madvise_wait_cycles%13lu\tavg = %.1f\n",
             globalizedThreadContention.madvise_wait_cycles_alloc_large,
             ((double)globalizedThreadContention.madvise_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.madvise_waits_alloc_large)));
    fprintf (thrData.output, ">>> small_dealloc_madvise\t\t%20lu\n", globalizedThreadContention.madvise_waits_free);
    fprintf (thrData.output, ">>> small_dealloc_madvise_wait_cycles%11lu\tavg = %.1f\n",
             globalizedThreadContention.madvise_wait_cycles_free,
             ((double)globalizedThreadContention.madvise_wait_cycles_free / safeDivisor(globalizedThreadContention.madvise_waits_free)));
    fprintf (thrData.output, ">>> large_dealloc_madvise\t\t%20lu\n", globalizedThreadContention.madvise_waits_free_large);
    fprintf (thrData.output, ">>> large_dealloc_madvise_wait_cycles%11lu\tavg = %.1f\n\n",
             globalizedThreadContention.madvise_wait_cycles_free_large,
             ((double)globalizedThreadContention.madvise_wait_cycles_free_large / safeDivisor(globalizedThreadContention.madvise_waits_free_large)));

    fprintf (thrData.output, ">>> small_alloc_munmap\t\t\t%20lu\n", globalizedThreadContention.munmap_waits_alloc);
    fprintf (thrData.output, ">>> small_alloc_munmap_wait_cycles%14lu\tavg = %.1f\n",
             globalizedThreadContention.munmap_wait_cycles_alloc,
             ((double)globalizedThreadContention.munmap_wait_cycles_alloc / safeDivisor(globalizedThreadContention.munmap_waits_alloc)));
    fprintf (thrData.output, ">>> large_alloc_munmap\t\t\t%20lu\n", globalizedThreadContention.munmap_waits_alloc_large);
    fprintf (thrData.output, ">>> large_alloc_munmap_wait_cycles%14lu\tavg = %.1f\n",
             globalizedThreadContention.munmap_wait_cycles_alloc_large,
             ((double)globalizedThreadContention.munmap_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.munmap_waits_alloc_large)));
    fprintf (thrData.output, ">>> small_dealloc_munmap\t\t%20lu\n", globalizedThreadContention.munmap_waits_free);
    fprintf (thrData.output, ">>> small_dealloc_munmap_wait_cycles%12lu\tavg = %.1f\n",
             globalizedThreadContention.munmap_wait_cycles_free,
             ((double)globalizedThreadContention.munmap_wait_cycles_free / safeDivisor(globalizedThreadContention.munmap_waits_free)));
    fprintf (thrData.output, ">>> large_dealloc_munmap\t\t%20lu\n", globalizedThreadContention.munmap_waits_free_large);
    fprintf (thrData.output, ">>> large_dealloc_munmap_wait_cycles%12lu\tavg = %.1f\n\n",
             globalizedThreadContention.munmap_wait_cycles_free_large,
             ((double)globalizedThreadContention.munmap_wait_cycles_free_large / safeDivisor(globalizedThreadContention.munmap_waits_free_large)));

    fprintf (thrData.output, ">>> small_alloc_mremap\t\t\t%20lu\n", globalizedThreadContention.mremap_waits_alloc);
    fprintf (thrData.output, ">>> small_alloc_mremap_wait_cycles%14lu\tavg = %.1f\n",
             globalizedThreadContention.mremap_wait_cycles_alloc,
             ((double)globalizedThreadContention.mremap_wait_cycles_alloc / safeDivisor(globalizedThreadContention.mremap_waits_alloc)));
    fprintf (thrData.output, ">>> large_alloc_mremap\t\t\t%20lu\n", globalizedThreadContention.mremap_waits_alloc_large);
    fprintf (thrData.output, ">>> large_alloc_mremap_wait_cycles%14lu\tavg = %.1f\n",
             globalizedThreadContention.mremap_wait_cycles_alloc_large,
             ((double)globalizedThreadContention.mremap_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.mremap_waits_alloc_large)));
    fprintf (thrData.output, ">>> small_dealloc_mremap\t\t%20lu\n", globalizedThreadContention.mremap_waits_free);
    fprintf (thrData.output, ">>> small_dealloc_mremap_wait_cycles%12lu\tavg = %.1f\n",
             globalizedThreadContention.mremap_wait_cycles_free,
             ((double)globalizedThreadContention.mremap_wait_cycles_free / safeDivisor(globalizedThreadContention.mremap_waits_free)));
    fprintf (thrData.output, ">>> large_dealloc_mremap\t\t%20lu\n", globalizedThreadContention.mremap_waits_free_large);
    fprintf (thrData.output, ">>> large_dealloc_mremap_wait_cycles%12lu\tavg = %.1f\n\n",
             globalizedThreadContention.mremap_wait_cycles_free_large,
             ((double)globalizedThreadContention.mremap_wait_cycles_free_large / safeDivisor(globalizedThreadContention.mremap_waits_free_large)));


    fprintf (thrData.output, ">>> small_alloc_mprotect\t\t%20lu\n", globalizedThreadContention.mprotect_waits_alloc);
    fprintf (thrData.output, ">>> small_alloc_mprotect_wait_cycles%12lu\tavg = %.1f\n",
             globalizedThreadContention.mprotect_wait_cycles_alloc,
             ((double)globalizedThreadContention.mprotect_wait_cycles_alloc / safeDivisor(globalizedThreadContention.mprotect_waits_alloc)));
    fprintf (thrData.output, ">>> large_alloc_mprotect\t\t%20lu\n", globalizedThreadContention.mprotect_waits_alloc_large);
    fprintf (thrData.output, ">>> large_alloc_mprotect_wait_cycles%12lu\tavg = %.1f\n",
             globalizedThreadContention.mprotect_wait_cycles_alloc_large,
             ((double)globalizedThreadContention.mprotect_wait_cycles_alloc_large / safeDivisor(globalizedThreadContention.mprotect_waits_alloc_large)));
    fprintf (thrData.output, ">>> small_dealloc_mprotect\t%20lu\n", globalizedThreadContention.mprotect_waits_free);
    fprintf (thrData.output, ">>> small_dealloc_mprotect_wait_cycles%10lu\tavg = %.1f\n",
             globalizedThreadContention.mprotect_wait_cycles_free,
             ((double)globalizedThreadContention.mprotect_wait_cycles_free / safeDivisor(globalizedThreadContention.mprotect_waits_free)));
    fprintf (thrData.output, ">>> large_dealloc_mprotect\t%20lu\n", globalizedThreadContention.mprotect_waits_free_large);
    fprintf (thrData.output, ">>> large_dealloc_mprotect_wait_cycles%10lu\tavg = %.1f\n\n",
             globalizedThreadContention.mprotect_wait_cycles_free_large,
             ((double)globalizedThreadContention.mprotect_wait_cycles_free_large / safeDivisor(globalizedThreadContention.mprotect_waits_free_large)));


		fprintf (thrData.output, ">>> critical_section\t\t\t\t%20lu\n",
						globalizedThreadContention.critical_section_counter);
		fprintf (thrData.output, ">>> critical_section_cycles\t\t%18lu\tavg = %.1f\n",
						globalizedThreadContention.critical_section_duration,
						((double)globalizedThreadContention.critical_section_duration / safeDivisor(globalizedThreadContention.critical_section_counter)));

		long realMem = max_mu.realAllocatedMemoryUsage;
		long realAllocMem = max_mu.realAllocatedMemoryUsage;
		long totalMem = max_mu.totalMemoryUsage;
		fprintf (thrData.output, "\n>>>>>>>>>>>>>>>>>>>>>>>>>> Total Memory Usage <<<<<<<<<<<<<<<<<<<<<<<<<\n");
    fprintf (thrData.output, ">>> Max Memory Usage in Threads:\n");
    fprintf (thrData.output, ">>> maxRealMemoryUsage\t\t%20zuK\n", maxRealMemoryUsage/1024);
    fprintf (thrData.output, ">>> maxRealAllocMemoryUsage%19zuK\n", maxRealAllocatedMemoryUsage/1024);
    fprintf (thrData.output, ">>> maxTotalMemoryUsage\t\t%20zuK\n\n", maxTotalMemoryUsage/1024);
    fprintf (thrData.output, ">>> Global Memory Usage:\n");
    fprintf (thrData.output, ">>> realMemoryUsage\t\t\t\t%20zuK\n", realMem/1024);
    fprintf (thrData.output, ">>> realAllocatedMemoryUsage%18zuK\n", realAllocMem/1024);
    fprintf (thrData.output, ">>> totalMemoryUsage\t\t\t%20zuK\n", totalMem/1024);
    MemoryWaste::reportMaxMemory(thrData.output, realMem, totalMem);
}

#endif
