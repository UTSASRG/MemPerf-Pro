#ifndef __RECORD_SCALE_HH__
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
#include "libmallocprof.h"
#include "hashmap.hh"
#include "memwaste.h"
#include "allocatingstatus.h"
#include "real.hh"
#include "shadowmemory.hh"
#include "memoryusage.h"
#include "structs.h"


extern HashMap <void*, DetailLockData, spinlock, PrivateHeap> lockUsage;

typedef int (*LockFunction) (void *);
typedef struct {
    int (*LockFunction)(void *);
} LockFunctionType;
LockFunctionType lockFunctions[NUM_OF_LOCKTYPES] = {
        (LockFunction)RealX::pthread_mutex_lock,
        (LockFunction)RealX::pthread_spin_lock,
        (LockFunction)RealX::pthread_mutex_trylock,
        (LockFunction)RealX::pthread_spin_trylock
};

typedef int (*UnlockFunction) (void *);
typedef struct {
    int (*UnlockFunction)(void *);
} UnlockFunctionType;
UnlockFunctionType unlockFunctions [NUM_OF_LOCKTYPES] = {
        (UnlockFunction)RealX::pthread_mutex_unlock,
        (UnlockFunction)RealX::pthread_spin_unlock,
        (UnlockFunction)RealX::pthread_mutex_unlock,
        (UnlockFunction)RealX::pthread_spin_unlock
};


extern "C" {

/* ************************Synchronization******************************** */
#define PTHREAD_LOCK_HANDLE(LOCKTYPE, LOCK) \
  if (AllocatingStatus::outsideTrackedAllocation()) { \
    return lockFunctions[LOCKTYPE].LockFunction((void *)LOCK); \
  } \
  \
  DetailLockData * detailLockData = lockUsage.find((void *)LOCK, sizeof(void *)); \
  if(detailLockData == nullptr)  { \
    detailLockData = lockUsage.insert((void*)LOCK, sizeof(void*), DetailLockData::newDetailLockData(LOCKTYPE)); \
    AllocatingStatus::recordANewLock(LOCKTYPE); \
  } \
  \
  AllocatingStatus::initForWritingOneLockData(LOCKTYPE, detailLockData); \
  if(detailLockData->aContentionHappening()) { \
    detailLockData->checkAndUpdateMaxNumOfContendingThreads(); \
    AllocatingStatus::recordALockContention(); \
  } \
  \
  uint64_t timeStart = rdtscp();\
  int result = lockFunctions[LOCKTYPE].LockFunction((void *)LOCK); \
  uint64_t timeStop = rdtscp(); \
  \
  AllocatingStatus::recordLockCallAndCycles(1, timeStop-timeStart); \
  AllocatingStatus::checkAndStartRecordingACriticalSection(); \
  return result;


int pthread_mutex_lock(pthread_mutex_t *mutex) {
    PTHREAD_LOCK_HANDLE(MUTEX, mutex);
}

int pthread_spin_lock(pthread_spinlock_t *lock) {
    PTHREAD_LOCK_HANDLE(SPIN, lock);
}

int pthread_spin_trylock(pthread_spinlock_t *lock) {
    PTHREAD_LOCK_HANDLE(SPINTRY, lock);
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    PTHREAD_LOCK_HANDLE(MUTEXTRY, mutex);
}


#define PTHREAD_UNLOCK_HANDLE(LOCKTYPE, lock) \
    if(AllocatingStatus::outsideTrackedAllocation()) { \
        return unlockFunctions[LOCKTYPE].UnlockFunction((void *)lock); \
    } \
    DetailLockData * detailLockData = lockUsage.find((void *)lock, sizeof(void *)); \
    detailLockData->quitFromContending(); \
    AllocatingStatus::checkAndStopRecordingACriticalSection(); \
    int result = unlockFunctions[LOCKTYPE].UnlockFunction((void *)lock); \
    return result;

// PTHREAD_MUTEX_UNLOCK
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  PTHREAD_UNLOCK_HANDLE(MUTEX, mutex);
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {
  PTHREAD_UNLOCK_HANDLE(SPIN, lock);
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
    void *new_address = va_arg(ap, void * );
    va_end(ap);

    if (AllocatingStatus::outsideTrackedAllocation()) {
        return RealX::mremap(old_address, old_size, new_size, flags, new_address);
    }

    uint64_t timeStart = rdtscp();
    void *ret = RealX::mremap(old_address, old_size, new_size, flags, new_address);
    uint64_t timeStop = rdtscp();

    AllocatingStatus::addToSystemCallData(MREMAP, SystemCallData{1, timeStop - timeStart});

    return ret;
}

#endif