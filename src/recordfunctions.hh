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
thread_local ThreadContention* current_tc;
thread_local bool threadLockWait;
thread_local uint lockTimeWaited;

// extern thread_local uint num_trylock;
// extern thread_local uint num_pthread_mutex_locks;
// extern thread_local uint threads;
extern thread_local uint malloc_mmaps;
extern thread_local uint size_sbrk;

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
    return RealX::pthread_mutex_lock (mutex);
  }

  //Have we encountered this lock before?
  LC* thisLock;
  uint64_t lockAddr = (uint64_t) mutex;
  if (lockUsage.find (lockAddr, &thisLock)) {
    thisLock->contention++;

    if (thisLock->contention > thisLock->maxContention)
      thisLock->maxContention = thisLock->contention;

    if (thisLock->contention > 1) {
      threadLockWait = true;
      lockTimeWaited = rdtscp();
    }
  }
  else { //Add lock to lockUsage hashmap
    lockUsage.insert(lockAddr, newLC());
  }

  // num_pthread_mutex_locks += 1;

  if (threadLockWait) {
    threadLockWait = false;
    current_tc->mutex_wait_cycles += (rdtscp() - lockTimeWaited);
  }

  uint64_t timeStart = rdtscp();
  //Aquire the lock
  int result = RealX::pthread_mutex_lock (mutex);
  uint64_t timeStop = rdtscp();

  current_tc->mutex_waits++;
  current_tc->mutex_wait_cycles += (timeStop - timeStart);
  if(current_tc->lock_counter == 0) {
    current_tc->critical_section_start = timeStop;
  }
  current_tc->lock_counter++;

  return result;
}

/*
// PTHREAD_MUTEX_TRYLOCK
int pthread_mutex_trylock (pthread_mutex_t *mutex) {
  if (!realInitialized) RealX::initializer();

  if (!mapsInitialized)
    return RealX::pthread_mutex_trylock (mutex);

  // if it is not used by allocation
  if (!inAllocation) {
    return RealX::pthread_mutex_trylock (mutex);
  }

  //Try to aquire the lock
  int result = RealX::pthread_mutex_trylock (mutex);
  if (result != 0) {
    num_trylock.fetch_add(1, relaxed);

    uint64_t lockAddr = reinterpret_cast <uint64_t> (mutex);
    //FIXME two array?

    current_tc->mutex_trylock_fails++;
  }

  return result;
}
*/

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
    malloc_mmaps += 1;

    current_tc->mmap_waits++;
    current_tc->mmap_wait_cycles += (timeStop - timeStart);

    mappings.insert(address, newMmapTuple(address, length, prot, 'a'));
  }
  //Need to check if selfmap.getInstance().getTextRegions() has
  //ran. If it hasn't, we can't call isAllocatorInCallStack()
  else if (selfmapInitialized && isAllocatorInCallStack()) {
    if (d_mmap) printf ("mmap allocator in callstack: length= %zu, prot= %d\n", length, prot);
    mappings.insert(address, newMmapTuple(address, length, prot, 's'));
  }

  // total_mmaps++;

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
    long returned = PAGESIZE * ShadowMemory::cleanupPages((uintptr_t)addr, length);
    if(current_tc->totalMemoryUsage > returned) {
      current_tc->totalMemoryUsage -= returned;
    }
  }

  uint64_t timeStart = rdtscp();
  int result = RealX::madvise(addr, length, advice);
  uint64_t timeStop = rdtscp();

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
  long returned = PAGESIZE * ShadowMemory::cleanupPages((intptr_t)addr, length);
  if(current_tc->totalMemoryUsage > returned) {
      current_tc->totalMemoryUsage -= returned;
  }

  uint64_t timeStart = rdtscp();
  int ret =  RealX::munmap(addr, length);
  uint64_t timeStop = rdtscp();

  current_tc->munmap_waits++;
  current_tc->munmap_wait_cycles += (timeStop - timeStart);

  mappings.erase((intptr_t)addr);
  // total_mmaps--;

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

    fprintf (thrData.output, "\n--------------------ThreadContention--------------------\n\n");

    size_t maxRealMemoryUsage = 0;
    size_t maxRealAllocatedMemoryUsage = 0;
    size_t maxTotalMemoryUsage = 0;

    ThreadContention globalizedThreadContention;

    for (int i=0; i<=threadcontention_index; i++) {
        ThreadContention* data = &all_threadcontention_array[i];

        globalizedThreadContention.tid += data->tid;
        globalizedThreadContention.mutex_waits += data->mutex_waits;
        globalizedThreadContention.mutex_wait_cycles += data->mutex_wait_cycles;
        globalizedThreadContention.mutex_trylock_fails += data->mutex_trylock_fails;
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

        long tmp_maxRealMemoryUsage = globalizedThreadContention.maxRealMemoryUsage == LONG_MIN ? 0 : data->maxRealMemoryUsage;
        long tmp_maxRealAllocatedMemoryUsage = globalizedThreadContention.maxRealAllocatedMemoryUsage == LONG_MIN ? 0 : data->maxRealAllocatedMemoryUsage;
        long tmp_maxTotalMemoryUsage = globalizedThreadContention.maxTotalMemoryUsage == LONG_MIN ? 0 : data->maxTotalMemoryUsage;

        maxRealMemoryUsage += tmp_maxRealMemoryUsage;
        maxRealAllocatedMemoryUsage += tmp_maxRealAllocatedMemoryUsage;
        maxTotalMemoryUsage += tmp_maxTotalMemoryUsage;
    }

    if (threadcontention_index > 0) {
        fprintf (thrData.output, ">>> mutex_waits          %d\n", (int)(globalizedThreadContention.mutex_waits / threadcontention_index));
        fprintf (thrData.output, ">>> mutex_wait_cycles    %d\n", (int)(globalizedThreadContention.mutex_wait_cycles / threadcontention_index));
        fprintf (thrData.output, ">>> mutex_trylock_fails  %d\n", (int)(globalizedThreadContention.mutex_trylock_fails / threadcontention_index));
        fprintf (thrData.output, ">>> mmap_waits           %d\n", (int)(globalizedThreadContention.mmap_waits / threadcontention_index));
        fprintf (thrData.output, ">>> mmap_wait_cycles     %d\n", (int)(globalizedThreadContention.mmap_wait_cycles / threadcontention_index));
        fprintf (thrData.output, ">>> sbrk_waits           %d\n", (int)(globalizedThreadContention.sbrk_waits / threadcontention_index));
        fprintf (thrData.output, ">>> sbrk_wait_cycles     %d\n", (int)(globalizedThreadContention.sbrk_wait_cycles / threadcontention_index));
        fprintf (thrData.output, ">>> madvise_waits        %d\n", (int)(globalizedThreadContention.madvise_waits / threadcontention_index));
        fprintf (thrData.output, ">>> madvise_wait_cycles  %d\n", (int)(globalizedThreadContention.madvise_wait_cycles / threadcontention_index));
        fprintf (thrData.output, ">>> munmap_waits         %d\n", (int)(globalizedThreadContention.munmap_waits / threadcontention_index));
        fprintf (thrData.output, ">>> munmap_wait_cycles   %d\n", (int)(globalizedThreadContention.munmap_wait_cycles / threadcontention_index));
        fprintf (thrData.output, ">>> mremap_waits         %d\n", (int)(globalizedThreadContention.mremap_waits / threadcontention_index));
        fprintf (thrData.output, ">>> mremap_wait_cycles   %d\n", (int)(globalizedThreadContention.mremap_wait_cycles / threadcontention_index));
        fprintf (thrData.output, ">>> mprotect_waits       %d\n", (int)(globalizedThreadContention.mprotect_waits / threadcontention_index));
        fprintf (thrData.output, ">>> mprotect_wait_cycle  %d\n\n", (int)(globalizedThreadContention.mprotect_wait_cycles / threadcontention_index));
        fprintf (thrData.output, ">>> critical_section_counter         %d\n", (int)(globalizedThreadContention.critical_section_counter / threadcontention_index));
        fprintf (thrData.output, ">>> critical_section_duration        %d\n\n", (int)(globalizedThreadContention.critical_section_duration / threadcontention_index));
    }

    fprintf (thrData.output, "\n--------------------Total Memory Usage--------------------\n\n");
    fprintf (thrData.output, ">>> Thread Counter:\n");
    fprintf (thrData.output, ">>> maxRealMemoryUsage           %lu\n", maxRealMemoryUsage);
    fprintf (thrData.output, ">>> maxRealAllocatedMemoryUsage  %lu\n", maxRealAllocatedMemoryUsage);
    fprintf (thrData.output, ">>> maxTotalMemoryUsage          %lu\n\n", maxTotalMemoryUsage);
    fprintf (thrData.output, ">>> Global Counter:\n");
    fprintf (thrData.output, ">>> realMemoryUsage           %lu\n", max_mu.realMemoryUsage);
    fprintf (thrData.output, ">>> realAllocatedMemoryUsage  %lu\n", max_mu.realAllocatedMemoryUsage);
    fprintf (thrData.output, ">>> totalMemoryUsage          %lu\n", max_mu.totalMemoryUsage);
}

#endif
