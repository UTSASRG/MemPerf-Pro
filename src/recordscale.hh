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
extern uint num_sbrk;
extern uint num_madvise;
extern uint malloc_mmaps;
extern size_t size_sbrk;
thread_local ThreadContention* threadContention;

//__thread thread_data thrData;
extern thread_local bool inAllocation;
extern thread_local bool inMmap;
extern bool realInitialized;
extern bool mapsInitialized;
extern bool selfmapInitialized;
extern initStatus profilerInitialized;

extern MemoryUsage max_mu;

//extern HashMap <uint64_t, MmapTuple*, spinlock> mappings;
extern HashMap <uint64_t, LockInfo*, spinlock> lockUsage;

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

/* ************************Synchronization******************************** */

// PTHREAD_MUTEX_LOCK
int pthread_mutex_lock(pthread_mutex_t *mutex) {
  if (!realInitialized) RealX::initializer();

  //PTHREAD_LOCK_HANDLE(MUTEX_LOCK, mutex);

  // if it is not used by allocation
  if (!inAllocation) {
    return RealX::pthread_mutex_lock(mutex);
  }

  // Have we encountered this lock before?
  LockInfo* thisLock;
  uint64_t lockAddr = (uint64_t) mutex;
    uint64_t timeStart, timeStop;
    int result;

    threadContention->mutex_waits++;


  int contention;
  if(lockUsage.find(lockAddr, &thisLock)) {
    contention  = thisLock->contention++;
  } else {
			// Add lock to lockUsage hashmap
			thisLock = newLockInfo(MUTEX);
			lockUsage.insertIfAbsent(lockAddr, thisLock);
			localTAD.num_mutex_locks++;

      contention = 1;
  }
   
  // Time the aquisition of the lock
  timeStart = rdtscp();
      result = RealX::pthread_mutex_lock(mutex);
      timeStop = rdtscp();

    if(contention > thisLock->maxContention) {
				thisLock->maxContention = contention;
		}

    threadContention->mutex_wait_cycles += (timeStop - timeStart);
    if(threadContention->lock_counter == 0) {
        threadContention->critical_section_start = timeStop;
    }

  thisLock->contention--;
    if(contention != 0) {
        thisLock->contention_times++;
      //thisLock->contentionCounter;
    }


  threadContention->lock_counter++;
    thisLock->times++;
  return result;
}

// PTHREAD_SPIN_TRYLOCK
int pthread_spin_trylock(pthread_spinlock_t *lock) {

  if(!realInitialized) {
			RealX::initializer();
	}

  if(!mapsInitialized || !inAllocation) {
    return RealX::pthread_spin_trylock(lock);
	}

  // Have we encountered this lock before?
  LockInfo* thisLock;
    uint64_t lockAddr = reinterpret_cast<uint64_t>(lock);

    threadContention->spin_trylock_waits++;

    if(lockUsage.find(lockAddr, &thisLock)) {

  } else {
      // Add lock to lockUsage hashmap
      thisLock = newLockInfo(SPIN_TRYLOCK, -1);
      lockUsage.insertIfAbsent(lockAddr, thisLock);
      localTAD.num_spin_trylocks++;
  }

    // Try to aquire the lock
  int result = RealX::pthread_spin_trylock(lock);
	uint64_t timeStop = rdtscp();
  if(result == 0) {
		if(threadContention->lock_counter == 0) {
				threadContention->critical_section_start = timeStop;
		}
		threadContention->lock_counter++;
      thisLock->times++;
  } else {
    threadContention->spin_trylock_fails++;
  }
  return result;
}

// PTHREAD_SPIN_LOCK
int pthread_spin_lock(pthread_spinlock_t *lock) {

  if(!realInitialized) RealX::initializer();

  // If we are not currently in an allocation then simply return
  if(!inAllocation) {
    return RealX::pthread_spin_lock(lock);
  }

  //Have we encountered this lock before?
  LockInfo* thisLock;
  uint64_t lockAddr = (uint64_t)lock;
  uint64_t timeStart, timeStop;
  int result;

    threadContention->spinlock_waits++;

  if(lockUsage.find(lockAddr, &thisLock)) {

    thisLock->contention++;

      // Time the aquisition of the lock
      timeStart = rdtscp();
      result = RealX::pthread_spin_lock(lock);
      timeStop = rdtscp();

    if(thisLock->contention > thisLock->maxContention) {
				thisLock->maxContention = thisLock->contention;

		}
  } else {
			// Add lock to lockUsage hashmap
			thisLock = newLockInfo(SPINLOCK);
			lockUsage.insertIfAbsent(lockAddr, thisLock);
			localTAD.num_spin_locks++;

      // Time the aquisition of the lock
      timeStart = rdtscp();
      result = RealX::pthread_spin_lock(lock);
      timeStop = rdtscp();
  }

    threadContention->spinlock_wait_cycles += (timeStop - timeStart);

	thisLock->contention--;
    if(thisLock->contention != 0) {
        thisLock->contention_times++;
    }

  if(threadContention->lock_counter == 0) {
    threadContention->critical_section_start = timeStop;
  }

  threadContention->lock_counter++;
    thisLock->times++;
  return result;
}

// PTHREAD_MUTEX_TRYLOCK
int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  if(!realInitialized) {
			RealX::initializer();
	}

  if(!mapsInitialized || !inAllocation) {
    return RealX::pthread_mutex_trylock(mutex);
	}

  // Have we encountered this lock before?
  LockInfo* thisLock;
	uint64_t lockAddr = reinterpret_cast<uint64_t>(mutex);

    threadContention->mutex_trylock_waits++;

  if(lockUsage.find(lockAddr, &thisLock)) {
  } else {
			// Add lock to lockUsage hashmap
			thisLock = newLockInfo(TRYLOCK, -1);
			lockUsage.insertIfAbsent(lockAddr, thisLock);
			localTAD.num_try_locks++;
  }

  // Try to aquire the lock

  int result = RealX::pthread_mutex_trylock(mutex);
	uint64_t timeStop = rdtscp();

  if(result == 0) {
		if(threadContention->lock_counter == 0) {
				threadContention->critical_section_start = timeStop;
		}
		threadContention->lock_counter++;
      thisLock->times++;

  } else {
      threadContention->mutex_trylock_fails++;
  }
  return result;
}

// PTHREAD_MUTEX_UNLOCK
int pthread_mutex_unlock(pthread_mutex_t *mutex) {

	// if it is  used by allocation
	if (inAllocation) {
			threadContention->lock_counter--;
			if(threadContention->lock_counter == 0) {
					uint64_t duration = rdtscp() - threadContention->critical_section_start;
					threadContention->critical_section_duration += duration;
          threadContention->critical_section_counter++;
			}
	}

  return RealX::pthread_mutex_unlock (mutex);
}

// PTHREAD_SPIN_UNLOCK
int pthread_spin_unlock(pthread_spinlock_t *lock) {
  if(!realInitialized) RealX::initializer();

	// if it is  used by allocation
	if(inAllocation) {
			threadContention->lock_counter--;
			if(threadContention->lock_counter == 0) {
					uint64_t duration = rdtscp() - threadContention->critical_section_start;
					threadContention->critical_section_duration += duration;
          threadContention->critical_section_counter++;
			}
	}

  return RealX::pthread_spin_unlock(lock);
}


/* ************************Synchronization End******************************** */

/* ************************Systemn Calls******************************** */

// MMAP
void * yymmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {

  if (!realInitialized) RealX::initializer();

  if (!mapsInitialized) return RealX::mmap (addr, length, prot, flags, fd, offset);

  if (inMmap) return RealX::mmap (addr, length, prot, flags, fd, offset);

  //thread_local
  inMmap = true;

  uint64_t timeStart = rdtscp();
  void* retval = RealX::mmap(addr, length, prot, flags, fd, offset);
  uint64_t timeStop = rdtscp();

  uint64_t address = (uint64_t)retval;

  //If this thread currently doing an allocation
  if (inAllocation) {
    malloc_mmaps++;
    localTAD.malloc_mmaps++;
    threadContention->mmap_waits++;
    threadContention->mmap_wait_cycles += (timeStop - timeStart);

    //mappings.insert(address, newMmapTuple(address, length, prot, 'a'));

	// Need to check if selfmap.getInstance().getTextRegions() has
	// ran. If it hasn't, we can't call isAllocatorInCallStack()
  } else if(selfmapInitialized && isAllocatorInCallStack()) {
    //mappings.insert(address, newMmapTuple(address, length, prot, 's'));
  }

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

	num_madvise++;
	localTAD.num_madvise++;
  threadContention->madvise_waits++;
  threadContention->madvise_wait_cycles += (timeStop - timeStart);

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

  threadContention->sbrk_waits++;
  threadContention->sbrk_wait_cycles += (timeStop - timeStart);

  localTAD.num_sbrk++;
  localTAD.size_sbrk += increment;

  num_sbrk++;
  size_sbrk += increment;

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

  threadContention->mprotect_waits++;
  threadContention->mprotect_wait_cycles += (timeStop - timeStart);

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

  threadContention->munmap_waits++;
  threadContention->munmap_wait_cycles += (timeStop - timeStart);

  //mappings.erase((intptr_t)addr);

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

  threadContention->mremap_waits++;
  threadContention->mremap_wait_cycles += (timeStop - timeStart);

//  MmapTuple* t;
//  if (mappings.find((intptr_t)old_address, &t)) {
//    if(ret == old_address) {
//      t->length = new_size;
//    } else {
//      mappings.erase((intptr_t)old_address);
//      mappings.insert((intptr_t)ret, newMmapTuple((intptr_t)ret, new_size, PROT_READ | PROT_WRITE, 'a'));
//    }
//  }

  return ret;
}

/* ************************Systemn Calls End******************************** */
};

void writeThreadContention() {
    fprintf(stderr, "writing\n");
    fprintf (thrData.output, "\n>>>>>>>>>>>>>>>>>>>>>>>>>> THREAD CONTENTIONS <<<<<<<<<<<<<<<<<<<<<<<<<<\n\n");

    long maxRealMemoryUsage = 0;
    long maxRealAllocatedMemoryUsage = 0;
    long maxTotalMemoryUsage = 0;

    ThreadContention globalizedThreadContention;

    for(int i = 0; i <= threadcontention_index; i++) {
        ThreadContention* data = &all_threadcontention_array[i];

        globalizedThreadContention.tid += data->tid;
        globalizedThreadContention.mutex_waits += data->mutex_waits;
        globalizedThreadContention.mutex_wait_cycles += data->mutex_wait_cycles;
        globalizedThreadContention.spinlock_waits += data->spinlock_waits;
        globalizedThreadContention.spinlock_wait_cycles += data->spinlock_wait_cycles;
        globalizedThreadContention.mutex_trylock_waits += data->mutex_trylock_waits;
        globalizedThreadContention.mutex_trylock_fails += data->mutex_trylock_fails;
        globalizedThreadContention.spin_trylock_waits += data->spin_trylock_waits;
        globalizedThreadContention.spin_trylock_fails += data->spin_trylock_fails;
        globalizedThreadContention.mmap_waits += data->mmap_waits;
        globalizedThreadContention.mmap_wait_cycles += data->mmap_wait_cycles;
        globalizedThreadContention.sbrk_waits += data->sbrk_waits;
        globalizedThreadContention.sbrk_wait_cycles += data->sbrk_wait_cycles;
        globalizedThreadContention.madvise_waits += data->madvise_waits;
        globalizedThreadContention.madvise_wait_cycles += data->madvise_wait_cycles;
        globalizedThreadContention.munmap_waits += data->munmap_waits;
        globalizedThreadContention.munmap_wait_cycles += data->munmap_wait_cycles;
        globalizedThreadContention.mremap_waits += data->mremap_waits;
        globalizedThreadContention.mremap_wait_cycles += data->mremap_wait_cycles;
        globalizedThreadContention.mprotect_waits += data->mprotect_waits;
        globalizedThreadContention.mprotect_wait_cycles += data->mprotect_wait_cycles;
        globalizedThreadContention.critical_section_counter += data->critical_section_counter;
        globalizedThreadContention.critical_section_duration += data->critical_section_duration;

        globalizedThreadContention.maxRealMemoryUsage = MAX(data->maxRealMemoryUsage, globalizedThreadContention.maxRealMemoryUsage);
        globalizedThreadContention.maxRealAllocatedMemoryUsage = MAX(data->maxRealAllocatedMemoryUsage, globalizedThreadContention.maxRealAllocatedMemoryUsage);
        globalizedThreadContention.maxTotalMemoryUsage = MAX(data->maxTotalMemoryUsage, globalizedThreadContention.maxTotalMemoryUsage);
    }

		maxRealMemoryUsage = globalizedThreadContention.maxRealMemoryUsage;
		maxRealAllocatedMemoryUsage = globalizedThreadContention.maxRealAllocatedMemoryUsage;
		maxTotalMemoryUsage = globalizedThreadContention.maxTotalMemoryUsage;

		fprintf (thrData.output, ">>> mutex\t\t\t%20lu\n",
						globalizedThreadContention.mutex_waits);
		fprintf (thrData.output, ">>> mutex_wait_cycles\t\t%20lu\tavg = %.1f\n",
						globalizedThreadContention.mutex_wait_cycles,
						((double)globalizedThreadContention.mutex_wait_cycles / safeDivisor(globalizedThreadContention.mutex_waits)));
		fprintf (thrData.output, ">>> mutex_trylock\t\t%20lu\n",
						globalizedThreadContention.mutex_trylock_waits);
		fprintf (thrData.output, ">>> mutex_trylock_fails\t\t%20lu\n",
						globalizedThreadContention.mutex_trylock_fails);
		fprintf (thrData.output, ">>> spinlock\t\t\t%20lu\n",
						globalizedThreadContention.spinlock_waits);
		fprintf (thrData.output, ">>> spinlock_wait_cycles\t%20lu\tavg = %.1f\n",
						globalizedThreadContention.spinlock_wait_cycles,
						((double)globalizedThreadContention.spinlock_wait_cycles / safeDivisor(globalizedThreadContention.spinlock_waits)));
		fprintf (thrData.output, ">>> spin_trylock\t\t%20lu\n",
						globalizedThreadContention.spin_trylock_waits);
		fprintf (thrData.output, ">>> spin_trylock_fails\t\t%20lu\n",
						globalizedThreadContention.spin_trylock_fails);
		fprintf (thrData.output, ">>> mmap\t\t\t%20lu\n",
						globalizedThreadContention.mmap_waits);
		fprintf (thrData.output, ">>> mmap_wait_cycles\t\t%20lu\tavg = %.1f\n",
						globalizedThreadContention.mmap_wait_cycles,
						((double)globalizedThreadContention.mmap_wait_cycles / safeDivisor(globalizedThreadContention.mmap_waits)));
		fprintf (thrData.output, ">>> sbrk\t\t\t%20lu\n",
						globalizedThreadContention.sbrk_waits);
		fprintf (thrData.output, ">>> sbrk_wait_cycles\t\t%20lu\tavg = %.1f\n",
						globalizedThreadContention.sbrk_wait_cycles,
						((double)globalizedThreadContention.sbrk_wait_cycles / safeDivisor(globalizedThreadContention.sbrk_waits)));
		fprintf (thrData.output, ">>> madvise\t\t\t%20lu\n",
						globalizedThreadContention.madvise_waits);
		fprintf (thrData.output, ">>> madvise_wait_cycles\t\t%20lu\tavg = %.1f\n",
						globalizedThreadContention.madvise_wait_cycles,
						((double)globalizedThreadContention.madvise_wait_cycles / safeDivisor(globalizedThreadContention.madvise_waits)));
		fprintf (thrData.output, ">>> munmap\t\t\t%20lu\n",
						globalizedThreadContention.munmap_waits);
		fprintf (thrData.output, ">>> munmap_wait_cycles\t\t%20lu\tavg = %.1f\n",
						globalizedThreadContention.munmap_wait_cycles,
						((double)globalizedThreadContention.munmap_wait_cycles / safeDivisor(globalizedThreadContention.munmap_waits)));
		fprintf (thrData.output, ">>> mremap\t\t\t%20lu\n",
						globalizedThreadContention.mremap_waits);
		fprintf (thrData.output, ">>> mremap_wait_cycles\t\t%20lu\tavg = %.1f\n",
						globalizedThreadContention.mremap_wait_cycles,
						((double)globalizedThreadContention.mremap_wait_cycles / safeDivisor(globalizedThreadContention.mremap_waits)));
		fprintf (thrData.output, ">>> mprotect\t\t\t%20lu\n",
						globalizedThreadContention.mprotect_waits);
		fprintf (thrData.output, ">>> mprotect_wait_cycles\t%20lu\tavg = %.1f\n\n",
						globalizedThreadContention.mprotect_wait_cycles,
						((double)globalizedThreadContention.mprotect_wait_cycles / safeDivisor(globalizedThreadContention.mprotect_waits)));
		fprintf (thrData.output, ">>> critical_section\t\t%20lu\n",
						globalizedThreadContention.critical_section_counter);
		fprintf (thrData.output, ">>> critical_section_cycles\t%20lu\tavg = %.1f\n\n",
						globalizedThreadContention.critical_section_duration,
						((double)globalizedThreadContention.critical_section_duration / safeDivisor(globalizedThreadContention.critical_section_counter)));

		fprintf (thrData.output, "\n>>>>>>>>>>>>>>>>>>>>>>>>>> Total Memory Usage <<<<<<<<<<<<<<<<<<<<<<<<<\n\n");
    fprintf (thrData.output, ">>> Max Memory Usage in Threads:\n");
    fprintf (thrData.output, ">>> maxRealMemoryUsage\t\t%20zuM\n", maxRealMemoryUsage/1024/1024);
    fprintf (thrData.output, ">>> maxRealAllocMemoryUsage\t%20zuM\n", maxRealAllocatedMemoryUsage/1024/1024);
    fprintf (thrData.output, ">>> maxTotalMemoryUsage\t\t%20zuM\n\n", maxTotalMemoryUsage/1024/1024);
    fprintf (thrData.output, ">>> Global Memory Usage:\n");
    fprintf (thrData.output, ">>> realMemoryUsage\t\t%20zuM\n", max_mu.realMemoryUsage/1024/1024);
    fprintf (thrData.output, ">>> realAllocatedMemoryUsage\t%20zuM\n", max_mu.realAllocatedMemoryUsage/1024/1024);
    fprintf (thrData.output, ">>> totalMemoryUsage\t\t%20zuM\n", max_mu.totalMemoryUsage/1024/1024);
    MemoryWaste::reportMaxMemory(thrData.output);
}

#endif
