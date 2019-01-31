#if !defined(MEASUREMENT_HH)
#define MEASUREMENT_HH

/*
 * @file   measurement.h
 * @brief  measure scalability issues
 * @author Hongyu Liu
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#define MAX_THREAD_NUMBER 2048

int threadcontention_index = -1;
ThreadContention all_threadcontention_array[MAX_THREAD_NUMBER];
extern thread_local thread_alloc_data localTAD;
extern std::atomic<uint> num_sbrk;
extern std::atomic<uint> num_madvise;
extern std::atomic<uint> malloc_mmaps;
extern std::atomic<std::size_t> size_sbrk;
thread_local ThreadContention* current_tc;

//__thread thread_data thrData;
extern thread_local bool inAllocation;
extern thread_local bool inMmap;
extern bool realInitialized;
extern bool mapsInitialized;
extern bool selfmapInitialized;
extern initStatus profilerInitialized;

extern const bool d_mmap;
extern const bool d_mprotect;

extern MemoryUsage max_mu;

extern HashMap <uint64_t, MmapTuple*, spinlock> mappings;
extern HashMap <uint64_t, LC*, spinlock> lockUsage;

void checkGlobalMemoryUsage();

extern "C" {

void setThreadContention() {
  int current_index = __atomic_add_fetch(&threadcontention_index, 1, __ATOMIC_RELAXED);
  if(current_index >= MAX_THREAD_NUMBER) {
    fprintf(stderr, "Please increase thread number: MAX_THREAD_NUMBER, %d\n", MAX_THREAD_NUMBER);
    abort();
  }
  current_tc = &all_threadcontention_array[current_index];
  current_tc->tid = gettid();
}

/* ************************Synchronization******************************** */

// PTHREAD_MUTEX_LOCK
int pthread_mutex_lock(pthread_mutex_t *mutex) {
  if (!realInitialized) RealX::initializer();

  // if it is not used by allocation
  if (!inAllocation) {
    return RealX::pthread_mutex_lock(mutex);
  }

  // Have we encountered this lock before?
  LC* thisLock;
  uint64_t lockAddr = (uint64_t) mutex;
  if(lockUsage.find(lockAddr, &thisLock)) {
    thisLock->contention++;

    if(thisLock->contention.load(relaxed) > thisLock->maxContention.load(relaxed)) {
				thisLock->maxContention.exchange(thisLock->contention.load(relaxed));
		}
  } else {
			// Add lock to lockUsage hashmap
			thisLock = newLC(MUTEX);
			lockUsage.insertIfAbsent(lockAddr, thisLock);
			localTAD.num_mutex_locks++;
  }

  // Time the aquisition of the lock
  uint64_t timeStart = rdtscp();
  int result = RealX::pthread_mutex_lock(mutex);
  uint64_t timeStop = rdtscp();

	thisLock->contention--;
  current_tc->mutex_waits++;
  current_tc->mutex_wait_cycles += (timeStop - timeStart);
  if(current_tc->lock_counter == 0) {
    current_tc->critical_section_start = timeStop;
  }
  current_tc->lock_counter++;

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
  LC* thisLock;
	uint64_t lockAddr = reinterpret_cast<uint64_t>(lock);
  if(lockUsage.find(lockAddr, &thisLock)) {
			/*
			thisLock->contention++;

			if(thisLock->contention.load(relaxed) > thisLock->maxContention.load(relaxed)) {
					thisLock->maxContention.exchange(thisLock->contention.load(relaxed));
			}
			*/
  } else {
			// Add lock to lockUsage hashmap
			thisLock = newLC(SPIN_TRYLOCK, -1);
			lockUsage.insertIfAbsent(lockAddr, thisLock);
			localTAD.num_spin_trylocks++;
  }

  // Try to aquire the lock
  current_tc->spin_trylock_waits++;
  int result = RealX::pthread_spin_trylock(lock);
	uint64_t timeStop = rdtscp();
  if(result == 0) {
		if(current_tc->lock_counter == 0) {
				current_tc->critical_section_start = timeStop;
		}
		current_tc->lock_counter++;
		//thisLock->contention--;
  } else {
    current_tc->spin_trylock_fails++;
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
  LC* thisLock;
  uint64_t lockAddr = (uint64_t)lock;
  if(lockUsage.find(lockAddr, &thisLock)) {
    thisLock->contention++;

    if(thisLock->contention.load(relaxed) > thisLock->maxContention.load(relaxed)) {
				thisLock->maxContention.exchange(thisLock->contention.load(relaxed));
		}
  } else {
			// Add lock to lockUsage hashmap
			thisLock = newLC(SPINLOCK);
			lockUsage.insertIfAbsent(lockAddr, thisLock);
			localTAD.num_spin_locks++;
  }

  // Time the aquisition of the lock
  uint64_t timeStart = rdtscp();
  int result = RealX::pthread_spin_lock(lock);
  uint64_t timeStop = rdtscp();

	thisLock->contention--;
  current_tc->spinlock_waits++;
  current_tc->spinlock_wait_cycles += (timeStop - timeStart);
  if(current_tc->lock_counter == 0) {
    current_tc->critical_section_start = timeStop;
  }
  current_tc->lock_counter++;

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
  LC* thisLock;
	uint64_t lockAddr = reinterpret_cast<uint64_t>(mutex);
  if(lockUsage.find(lockAddr, &thisLock)) {
			/*
			thisLock->contention++;

			if(thisLock->contention.load(relaxed) > thisLock->maxContention.load(relaxed)) {
					thisLock->maxContention.exchange(thisLock->contention.load(relaxed));
			}
			*/
  } else {
			// Add lock to lockUsage hashmap
			thisLock = newLC(TRYLOCK, -1);
			lockUsage.insertIfAbsent(lockAddr, thisLock);
			localTAD.num_try_locks++;
  }

  // Try to aquire the lock
  current_tc->mutex_trylock_waits++;
  int result = RealX::pthread_mutex_trylock(mutex);
	uint64_t timeStop = rdtscp();
  if(result == 0) {
		if(current_tc->lock_counter == 0) {
				current_tc->critical_section_start = timeStop;
		}
		current_tc->lock_counter++;
		//thisLock->contention--;
  } else {
    current_tc->mutex_trylock_fails++;
  }

  return result;
}

// PTHREAD_MUTEX_UNLOCK
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  if (!realInitialized) RealX::initializer();

	// if it is  used by allocation
	if (inAllocation) {
			current_tc->lock_counter--;
			if(current_tc->lock_counter == 0) {
					uint64_t duration = rdtscp() - current_tc->critical_section_start;
					current_tc->critical_section_duration += duration;
          current_tc->critical_section_counter++;
			}
	}

  return RealX::pthread_mutex_unlock (mutex);
}

// PTHREAD_SPIN_UNLOCK
int pthread_spin_unlock(pthread_spinlock_t *lock) {
  if(!realInitialized) RealX::initializer();

	// if it is  used by allocation
	if(inAllocation) {
			current_tc->lock_counter--;
			if(current_tc->lock_counter == 0) {
					uint64_t duration = rdtscp() - current_tc->critical_section_start;
					current_tc->critical_section_duration += duration;
          current_tc->critical_section_counter++;
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
    if (d_mmap) printf ("mmap direct from allocation function: length= %zu, prot= %d\n", length, prot);

    malloc_mmaps++;
    localTAD.malloc_mmaps++;
    current_tc->mmap_waits++;
    current_tc->mmap_wait_cycles += (timeStop - timeStart);

    mappings.insert(address, newMmapTuple(address, length, prot, 'a'));

	// Need to check if selfmap.getInstance().getTextRegions() has
	// ran. If it hasn't, we can't call isAllocatorInCallStack()
  } else if(selfmapInitialized && isAllocatorInCallStack()) {
    if(d_mmap) printf ("mmap allocator in callstack: length= %zu, prot= %d\n", length, prot);
    mappings.insert(address, newMmapTuple(address, length, prot, 's'));
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
    if(current_tc->totalMemoryUsage > returned) {
      current_tc->totalMemoryUsage -= returned;
    }
  }

  uint64_t timeStart = rdtscp();
  int result = RealX::madvise(addr, length, advice);
  uint64_t timeStop = rdtscp();

	num_madvise++;
	localTAD.num_madvise++;
  current_tc->madvise_waits++;
  current_tc->madvise_wait_cycles += (timeStop - timeStart);

  return result;
}

// SBRK
void *sbrk(intptr_t increment){
  if (!realInitialized) RealX::initializer();
  if(profilerInitialized != INITIALIZED || !inAllocation) return RealX::sbrk(increment);

  uint64_t timeStart = rdtscp();
  void *retptr = RealX::sbrk(increment);
  uint64_t timeStop = rdtscp();

  current_tc->sbrk_waits++;
  current_tc->sbrk_wait_cycles += (timeStop - timeStart);

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

  current_tc->mprotect_waits++;
  current_tc->mprotect_wait_cycles += (timeStop - timeStart);

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
  if(current_tc->totalMemoryUsage > returned) {
      current_tc->totalMemoryUsage -= returned;
  }

  uint64_t timeStart = rdtscp();
  int ret =  RealX::munmap(addr, length);
  uint64_t timeStop = rdtscp();

  current_tc->munmap_waits++;
  current_tc->munmap_wait_cycles += (timeStop - timeStart);

  mappings.erase((intptr_t)addr);

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

  current_tc->mremap_waits++;
  current_tc->mremap_wait_cycles += (timeStop - timeStart);

  MmapTuple* t;
  if (mappings.find((intptr_t)old_address, &t)) {
    if(ret == old_address) {
      t->length = new_size;
    } else {
      mappings.erase((intptr_t)old_address);
      mappings.insert((intptr_t)ret, newMmapTuple((intptr_t)ret, new_size, PROT_READ | PROT_WRITE, 'a'));
    }
  }

  return ret;
}

/* ************************Systemn Calls End******************************** */
};

void writeThreadContention() {

    fprintf (thrData.output, "\n>>>>>>>>>>>>>>>>>>>>>>>>>> Thread Contention <<<<<<<<<<<<<<<<<<<<<<<<<<\n\n");

    size_t maxRealMemoryUsage = 0;
    size_t maxRealAllocatedMemoryUsage = 0;
    size_t maxTotalMemoryUsage = 0;

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

		fprintf (thrData.output, ">>> mutex_waits                %20lu\n",
						globalizedThreadContention.mutex_waits);
		fprintf (thrData.output, ">>> mutex_wait_cycles          %20lu    avg = %.1f\n",
						globalizedThreadContention.mutex_wait_cycles,
						((double)globalizedThreadContention.mutex_wait_cycles / safeDivisor(globalizedThreadContention.mutex_waits)));
		fprintf (thrData.output, ">>> mutex_trylock_waits        %20lu\n",
						globalizedThreadContention.mutex_trylock_waits);
		fprintf (thrData.output, ">>> mutex_trylock_fails        %20lu\n",
						globalizedThreadContention.mutex_trylock_fails);
		fprintf (thrData.output, ">>> spinlock_waits             %20lu\n",
						globalizedThreadContention.spinlock_waits);
		fprintf (thrData.output, ">>> spinlock_wait_cycles       %20lu    avg = %.1f\n",
						globalizedThreadContention.spinlock_wait_cycles,
						((double)globalizedThreadContention.spinlock_wait_cycles / safeDivisor(globalizedThreadContention.spinlock_waits)));
		fprintf (thrData.output, ">>> spin_trylock_waits         %20lu\n",
						globalizedThreadContention.spin_trylock_waits);
		fprintf (thrData.output, ">>> spin_trylock_fails         %20lu\n",
						globalizedThreadContention.spin_trylock_fails);
		fprintf (thrData.output, ">>> mmap_waits                 %20lu\n",
						globalizedThreadContention.mmap_waits);
		fprintf (thrData.output, ">>> mmap_wait_cycles           %20lu    avg = %.1f\n",
						globalizedThreadContention.mmap_wait_cycles,
						((double)globalizedThreadContention.mmap_wait_cycles / safeDivisor(globalizedThreadContention.mmap_waits)));
		fprintf (thrData.output, ">>> sbrk_waits                 %20lu\n",
						globalizedThreadContention.sbrk_waits);
		fprintf (thrData.output, ">>> sbrk_wait_cycles           %20lu    avg = %.1f\n",
						globalizedThreadContention.sbrk_wait_cycles,
						((double)globalizedThreadContention.sbrk_wait_cycles / safeDivisor(globalizedThreadContention.sbrk_waits)));
		fprintf (thrData.output, ">>> madvise_waits              %20lu\n",
						globalizedThreadContention.madvise_waits);
		fprintf (thrData.output, ">>> madvise_wait_cycles        %20lu    avg = %.1f\n",
						globalizedThreadContention.madvise_wait_cycles,
						((double)globalizedThreadContention.madvise_wait_cycles / safeDivisor(globalizedThreadContention.madvise_waits)));
		fprintf (thrData.output, ">>> munmap_waits               %20lu\n",
						globalizedThreadContention.munmap_waits);
		fprintf (thrData.output, ">>> munmap_wait_cycles         %20lu    avg = %.1f\n",
						globalizedThreadContention.munmap_wait_cycles,
						((double)globalizedThreadContention.munmap_wait_cycles / safeDivisor(globalizedThreadContention.munmap_waits)));
		fprintf (thrData.output, ">>> mremap_waits               %20lu\n",
						globalizedThreadContention.mremap_waits);
		fprintf (thrData.output, ">>> mremap_wait_cycles         %20lu    avg = %.1f\n",
						globalizedThreadContention.mremap_wait_cycles,
						((double)globalizedThreadContention.mremap_wait_cycles / safeDivisor(globalizedThreadContention.mremap_waits)));
		fprintf (thrData.output, ">>> mprotect_waits             %20lu\n",
						globalizedThreadContention.mprotect_waits);
		fprintf (thrData.output, ">>> mprotect_wait_cycle        %20lu    avg = %.1f\n\n",
						globalizedThreadContention.mprotect_wait_cycles,
						((double)globalizedThreadContention.mprotect_wait_cycles / safeDivisor(globalizedThreadContention.mprotect_waits)));
		fprintf (thrData.output, ">>> critical_section_counter   %20lu\n",
						globalizedThreadContention.critical_section_counter);
		fprintf (thrData.output, ">>> critical_section_duration  %20lu    avg = %.1f\n\n",
						globalizedThreadContention.critical_section_duration,
						((double)globalizedThreadContention.critical_section_duration / safeDivisor(globalizedThreadContention.critical_section_counter)));

		fprintf (thrData.output, "\n>>>>>>>>>>>>>>>>>>>>>>>>>> Total Memory Usage <<<<<<<<<<<<<<<<<<<<<<<<<\n\n");
    fprintf (thrData.output, ">>> Thread Counter:\n");
    fprintf (thrData.output, ">>> maxRealMemoryUsage         %20zu\n", maxRealMemoryUsage);
    fprintf (thrData.output, ">>> maxRealAllocMemoryUsage    %20zu\n", maxRealAllocatedMemoryUsage);
    fprintf (thrData.output, ">>> maxTotalMemoryUsage        %20zu\n\n", maxTotalMemoryUsage);
    fprintf (thrData.output, ">>> Global Counter:\n");
    fprintf (thrData.output, ">>> realMemoryUsage            %20zu\n", max_mu.realMemoryUsage);
    fprintf (thrData.output, ">>> realAllocatedMemoryUsage   %20zu\n", max_mu.realAllocatedMemoryUsage);
    fprintf (thrData.output, ">>> totalMemoryUsage           %20zu\n", max_mu.totalMemoryUsage);
}

#endif
