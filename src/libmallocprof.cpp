#include "libmallocprof.h"


thread_local HashMap <void *, DetailLockData, PrivateHeap> lockUsage;
HashMap <void *, DetailLockData, PrivateHeap> globalLockUsage;
#ifdef OPEN_BACKTRACE
HashMap <uint8_t, BackTraceMemory, PrivateHeap> BTMemMap;
HashMap <uint8_t, BackTraceMemory, PrivateHeap> BTMemMapRecord;
#endif
HashMap <void*, ObjectStatus, PrivateHeap> objStatusMap;

//// pre-init private allocator memory
typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main_mallocprof;

extern "C" {
//	 Function prototypes
	void exitHandler();

//	 Function aliases
	void free(void *) __attribute__ ((weak, alias("yyfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("yycalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("yymalloc")));
	void * realloc(void *, size_t) __attribute__ ((weak, alias("yyrealloc")));
	void * memalign(size_t, size_t) __attribute__ ((weak, alias("yymemalign")));
	int posix_memalign(void **, size_t, size_t) __attribute__ ((weak,
				alias("yyposix_memalign")));
    void * mmap(void *addr, size_t length, int prot, int flags,
            int fd, off_t offset) __attribute__ ((weak, alias("yymmap")));
}

void exitHandler() {

    ProgramStatus::setBeginConclusionTrue();
    Predictor::outsideCyclesStop();
    Predictor::outsideCountingEventsStop();
    Predictor::stopSerial();
//    MemoryUsage::endCheck();
#ifdef OPEN_SAMPLING_EVENT
	stopSampling();
#endif
#ifdef OPEN_COUNTING_EVENT
    stopCounting();
#endif

    GlobalStatus::globalize();
    GlobalStatus::printOutput();
//    GlobalStatus::printForMatrix();


}

// MallocProf's main function
int libmallocprof_main(int argc, char ** argv, char ** envp) {
#ifdef PRINT_LEAK_OBJECTS
    selfmap::getInstance().getGlobalRegions();
#endif
    RealX::initializer();
    ShadowMemory::initialize();
    ThreadLocalStatus::setStackStartAddress(&argc);
    ThreadLocalStatus::addARunningThread();
    ThreadLocalStatus::getARunningThreadIndex();

#ifdef OPEN_SAMPLING_FOR_ALLOCS
    ThreadLocalStatus::setRandomPeriodForAllocations(RANDOM_PERIOD_FOR_ALLOCS);
#endif

#ifdef OPEN_CPU_BINDING
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(ThreadLocalStatus::runningThreadIndex%40, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
    {
        fprintf(stderr, "warning: could not set CPU affinity\n");
        abort();
    }
#endif

    ProgramStatus::initIO(argv[0]);
    lockUsage.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_LOCK_NUM);
    globalLockUsage.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_LOCK_NUM);
    MemoryWaste::initialize();
#ifdef OPEN_BACKTRACE
    Backtrace::init();
#endif
    MyMalloc::initializeForThreadLocalXthreadMemory(ThreadLocalStatus::runningThreadIndex);
    MyMalloc::initializeForThreadLocalHashMemory(ThreadLocalStatus::runningThreadIndex);
    MyMalloc::initializeForThreadLocalShadowMemory(ThreadLocalStatus::runningThreadIndex);
    MyMalloc::initializeForThreadLocalMemory();
    Predictor::globalInit();
#ifdef OPEN_SAMPLING_EVENT
    initPMU();
#endif

    Predictor::outsideCountingEventsStart();
    Predictor::outsideCycleStart();
    ProgramStatus::setProfilerInitializedTrue();

    atexit(exitHandler);

	return real_main_mallocprof (argc, argv, envp);
}

extern "C" int __libc_start_main(main_fn_t, int, char **, void (*)(), void (*)(), void (*)(), void *) __attribute__((weak, alias("libmallocprof_libc_start_main")));

extern "C" int libmallocprof_libc_start_main(main_fn_t main_fn, int argc, char ** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(), void * stack_end) {
	auto real_libc_start_main = (decltype(__libc_start_main) *)dlsym(RTLD_NEXT, "__libc_start_main");
	real_main_mallocprof = main_fn;
	return real_libc_start_main(libmallocprof_main, argc, argv, init, fini, rtld_fini, stack_end);
}


//// Memory management functions
extern "C" {
	void * yymalloc(size_t sz) {
        if(sz == 0) {
            return NULL;
        }
        if(ProgramStatus::profilerNotInitialized()) {
            return MyMalloc::malloc(sz);
        }
        if(!AllocatingStatus::outsideTrackedAllocation() || ProgramStatus::conclusionHasStarted()) {
            return RealX::malloc(sz);
        }
#ifdef OPEN_COUNTING_EVENT
        if(AllocatingStatus::isFirstFunction()) {
#ifdef OPEN_SAMPLING_EVENT
            pauseSampling();
#endif
            setupCounting();
#ifdef OPEN_SAMPLING_EVENT
            restartSampling();
#endif
        }
#endif

        Predictor::outsideCyclesStop();
        AllocatingStatus::updateAllocatingStatusBeforeRealFunction(MALLOC, sz);
//        if(ThreadLocalStatus::runningThreadIndex == 1) {
//            fprintf(stderr, "sz = %lu\n", sz);
//        }
		void * object = RealX::malloc(sz);
        AllocatingStatus::updateAllocatingStatusAfterRealFunction(object);
        AllocatingStatus::updateAllocatingInfoToThreadLocalData();
        AllocatingStatus::updateAllocatingInfoToPredictor();
        Predictor::outsideCycleStart();
//        fprintf(stderr, "%lu allocate object %p\n", ThreadLocalStatus::runningThreadIndex, object);
        return object;

	}


	void * yycalloc(size_t nelem, size_t elsize) {

		if((nelem * elsize) == 0) {
				return NULL;
		}

		if (ProgramStatus::profilerNotInitialized()) {
            return MyMalloc::malloc(nelem*elsize);
        }
        if(!AllocatingStatus::outsideTrackedAllocation() || ProgramStatus::conclusionHasStarted()) {
            return RealX::calloc(nelem, elsize);
        }

        Predictor::outsideCyclesStop();
        AllocatingStatus::updateAllocatingStatusBeforeRealFunction(CALLOC, nelem*elsize);
        void * object = RealX::calloc(nelem, elsize);
        AllocatingStatus::updateAllocatingStatusAfterRealFunction(object);
        AllocatingStatus::updateAllocatingInfoToThreadLocalData();
        AllocatingStatus::updateAllocatingInfoToPredictor();
        Predictor::outsideCycleStart();
		return object;
	}


	void yyfree(void * ptr) {
        if(ptr == nullptr) return;

        if(MyMalloc::ifInProfilerMemoryThenFree(ptr) || ProgramStatus::profilerNotInitialized()) {
            return;
        }

        if(!AllocatingStatus::outsideTrackedAllocation() || ProgramStatus::conclusionHasStarted()) {
            return RealX::free(ptr);
        }
#ifdef OPEN_COUNTING_EVENT
        if(AllocatingStatus::isFirstFunction()) {
#ifdef OPEN_SAMPLING_EVENT
            pauseSampling();
#endif
            setupCounting();
#ifdef OPEN_SAMPLING_EVENT
            restartSampling();
#endif
        }
#endif
//        fprintf(stderr, "%lu free object %p\n", ThreadLocalStatus::runningThreadIndex, ptr);
        Predictor::outsideCyclesStop();
        AllocatingStatus::updateFreeingStatusBeforeRealFunction(FREE, ptr);
        RealX::free(ptr);
        AllocatingStatus::updateFreeingStatusAfterRealFunction();
        AllocatingStatus::updateAllocatingInfoToThreadLocalData();
        AllocatingStatus::updateAllocatingInfoToPredictor();
        Predictor::outsideCycleStart();
    }


	void * yyrealloc(void * ptr, size_t sz) {

        if (ProgramStatus::profilerNotInitialized()) {
            MyMalloc::ifInProfilerMemoryThenFree(ptr);
            return MyMalloc::malloc(sz);
        }

        if(MyMalloc::ifInProfilerMemoryThenFree(ptr)) {
            return RealX::malloc(sz);
        }

        if(!AllocatingStatus::outsideTrackedAllocation() || ProgramStatus::conclusionHasStarted()) {
            return RealX::realloc(ptr, sz);
        }

        Predictor::outsideCyclesStop();
		if(ptr) {
            AllocatingStatus::updateFreeingStatusBeforeRealFunction(REALLOC, ptr);
        }
        AllocatingStatus::updateAllocatingStatusBeforeRealFunction(REALLOC, sz);
        void * object = RealX::realloc(ptr, sz);
        AllocatingStatus::updateAllocatingStatusAfterRealFunction(object);
        AllocatingStatus::updateAllocatingInfoToThreadLocalData();
        AllocatingStatus::updateAllocatingInfoToPredictor();
        Predictor::outsideCycleStart();
        return object;
	}


	int yyposix_memalign(void **memptr, size_t alignment, size_t size) {
        if(size == 0) {
            return 0;
        }

        if(!AllocatingStatus::outsideTrackedAllocation() || ProgramStatus::profilerNotInitialized() || ProgramStatus::conclusionHasStarted()) {
            return RealX::posix_memalign(memptr, alignment, size);
        }

        Predictor::outsideCyclesStop();
        AllocatingStatus::updateAllocatingStatusBeforeRealFunction(POSIX_MEMALIGN, size);
        int retval = RealX::posix_memalign(memptr, alignment, size);
        AllocatingStatus::updateAllocatingStatusAfterRealFunction(*memptr);
        AllocatingStatus::updateAllocatingInfoToThreadLocalData();
        AllocatingStatus::updateAllocatingInfoToPredictor();
        Predictor::outsideCycleStart();
        return retval;

	}


 void * yymemalign(size_t alignment, size_t size) {
     if(size == 0) {
         return NULL;
     }

     if(ProgramStatus::profilerNotInitialized()) {
         return MyMalloc::malloc(size);
     }

     if(!AllocatingStatus::outsideTrackedAllocation() || ProgramStatus::conclusionHasStarted()) {
         return RealX::memalign(alignment, size);
     }

     Predictor::outsideCyclesStop();
     AllocatingStatus::updateAllocatingStatusBeforeRealFunction(MEMALIGN, size);
     void * object = RealX::memalign(alignment, size);
     AllocatingStatus::updateAllocatingStatusAfterRealFunction(object);
     AllocatingStatus::updateAllocatingInfoToThreadLocalData();
     AllocatingStatus::updateAllocatingInfoToPredictor();
     Predictor::outsideCycleStart();
     return object;
	}
}

void * operator new (size_t sz) {
	return yymalloc(sz);
}

void * operator new (size_t sz, const std::nothrow_t&) throw() {
	return yymalloc(sz);
}

void operator delete (void * ptr) __THROW {
	yyfree (ptr);
}

void * operator new[] (size_t sz) {
	return yymalloc(sz);
}

void * operator new[] (size_t sz, const std::nothrow_t&)
  throw()
 {
		 return yymalloc(sz);
}

void operator delete[] (void * ptr) __THROW {
	yyfree (ptr);
}

extern "C" {
void *yymmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (!realInitialized) RealX::initializer();
    if (ProgramStatus::profilerNotInitialized()) {
        void * retval =  RealX::mmap(addr, length, prot, flags, fd, offset);
#ifdef ENABLE_HP
        if(flags & MAP_HUGETLB) {
            ShadowMemory::HPBeforeInit.add((uintptr_t)retval, length);
        }
#ifdef ENABLE_THP
        else if((uint64_t)retval%(2*ONE_MB) == 0 && length >= 2*ONE_MB) {
            ShadowMemory::THPBeforeInit.add((uintptr_t)retval, length);
        }
#endif
#endif
        return retval;
    }

    if(AllocatingStatus::outsideTrackedAllocation() || !AllocatingStatus::sampledForCountingEvent) {
        void * retval = RealX::mmap(addr, length, prot, flags, fd, offset);
#ifdef ENABLE_HP
        if(flags & MAP_HUGETLB) {
            ShadowMemory::setHugePages((uintptr_t) retval, length);
        }
#ifdef ENABLE_THP
        else if((uint64_t)retval%(2*ONE_MB) == 0 && length >= 2*ONE_MB) {
            ShadowMemory::setTransparentHugePages((uintptr_t) retval, length);
        }
#endif
#endif

        return retval;
    }

    uint64_t timeStart = rdtscp();
    void *retval = RealX::mmap(addr, length, prot, flags, fd, offset);
    uint64_t timeStop = rdtscp();
#ifdef ENABLE_HP
    if(flags & MAP_HUGETLB) {
        ShadowMemory::setHugePages((uintptr_t) retval, length);
    }
#ifdef ENABLE_THP
    else if((uint64_t)retval%(2*ONE_MB) == 0 && length >= 2*ONE_MB) {
        ShadowMemory::setTransparentHugePages((uintptr_t) retval, length);
    }
#endif
#endif

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::addOneSyscallToSyscallData(MMAP, timeStop - timeStart);

    return retval;
}

int madvise(void *addr, size_t length, int advice) {
    if (!realInitialized) RealX::initializer();
    if (AllocatingStatus::outsideTrackedAllocation()) {
        return RealX::madvise(addr, length, advice);
    }
    if (advice == MADV_DONTNEED) {
        size_t cleanedPageSize = ShadowMemory::cleanupPages((uintptr_t) addr, length);
        MemoryUsage::subTotalSizeFromMemoryUsage(cleanedPageSize);
    }

    if (!AllocatingStatus::sampledForCountingEvent) {
        int result = RealX::madvise(addr, length, advice);
#ifdef ENABLE_HP
        if(advice == MADV_HUGEPAGE) {
            ShadowMemory::setHugePages((uintptr_t) addr, length);
        }
        if(advice == MADV_NOHUGEPAGE) {
            ShadowMemory::cancelHugePages((uintptr_t) addr, length);
        }
#endif
        return result;
    }

    uint64_t timeStart = rdtscp();
    int result = RealX::madvise(addr, length, advice);
    uint64_t timeStop = rdtscp();

#ifdef ENABLE_HP
    if(advice == MADV_HUGEPAGE) {
        ShadowMemory::setHugePages((uintptr_t) addr, length);
    }
    if(advice == MADV_NOHUGEPAGE) {
        ShadowMemory::cancelHugePages((uintptr_t) addr, length);
    }
#endif

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::addOneSyscallToSyscallData(MADVISE, timeStop - timeStart);

    return result;
}

void *sbrk(intptr_t increment) {
    if (!realInitialized) RealX::initializer();
    if (AllocatingStatus::outsideTrackedAllocation() || !AllocatingStatus::sampledForCountingEvent) {
        return RealX::sbrk(increment);
    }

    uint64_t timeStart = rdtscp();
    void *retptr = RealX::sbrk(increment);
    uint64_t timeStop = rdtscp();

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::addOneSyscallToSyscallData(SBRK, timeStop - timeStart);

    return retptr;
}

int mprotect(void *addr, size_t len, int prot) {
    if (!realInitialized) RealX::initializer();
    if (AllocatingStatus::outsideTrackedAllocation() || !AllocatingStatus::sampledForCountingEvent) {
        return RealX::mprotect(addr, len, prot);
    }

    uint64_t timeStart = rdtscp();
    int ret = RealX::mprotect(addr, len, prot);
    uint64_t timeStop = rdtscp();

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::addOneSyscallToSyscallData(MPROTECT, timeStop - timeStart);

    return ret;
}

int munmap(void *addr, size_t length) {

    if (!realInitialized) RealX::initializer();

    if (AllocatingStatus::outsideTrackedAllocation()) {
        return RealX::munmap(addr, length);
    }
    size_t cleanedPageSize = ShadowMemory::cleanupPages((intptr_t) addr, length);
    MemoryUsage::subTotalSizeFromMemoryUsage(cleanedPageSize);

    if (!AllocatingStatus::sampledForCountingEvent) {
        return RealX::munmap(addr, length);
    }

    uint64_t timeStart = rdtscp();
    int ret = RealX::munmap(addr, length);
    uint64_t timeStop = rdtscp();

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::addOneSyscallToSyscallData(MUNMAP, timeStop - timeStart);


    return ret;
}

void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ...) {
    if (!realInitialized) RealX::initializer();
    va_list ap;
    va_start(ap, flags);
    void *new_address = va_arg(ap, void * );
    va_end(ap);

    if (AllocatingStatus::outsideTrackedAllocation() || !AllocatingStatus::sampledForCountingEvent) {
        void* ret = RealX::mremap(old_address, old_size, new_size, flags, new_address);
        return ret;
    }

    uint64_t timeStart = rdtscp();
    void *ret = RealX::mremap(old_address, old_size, new_size, flags, new_address);
    uint64_t timeStop = rdtscp();

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::addOneSyscallToSyscallData(MREMAP, timeStop - timeStart);


    return ret;
}
}

extern "C" {
int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (AllocatingStatus::outsideTrackedAllocation() || !AllocatingStatus::sampledForCountingEvent) {
        if (!realInitialized) RealX::initializer();
        return RealX::pthread_mutex_lock(mutex);
    }

    DetailLockData * detailLockData = lockUsage.find((void *)mutex, sizeof(void *));
    if(detailLockData == nullptr)  {
        detailLockData = lockUsage.insert((void*)mutex, sizeof(void*), DetailLockData::newDetailLockData(MUTEX));
        AllocatingStatus::recordANewLock(MUTEX);
    }

    AllocatingStatus::initForWritingOneLockData(MUTEX, detailLockData);

    if(mutex->__data.__lock) {
        AllocatingStatus::recordALockContention();
    }

    uint64_t timeStart = rdtscp();
    int result = RealX::pthread_mutex_lock(mutex);
    uint64_t timeStop = rdtscp();

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::recordLockCallAndCycles(1, timeStop-timeStart);
    AllocatingStatus::checkAndStartRecordingACriticalSection();

    return result;
}

int pthread_spin_lock(pthread_spinlock_t *lock) {
    if (AllocatingStatus::outsideTrackedAllocation()|| !AllocatingStatus::sampledForCountingEvent) {
        if (!realInitialized) RealX::initializer();
        return RealX::pthread_spin_lock(lock);
    }
    DetailLockData * detailLockData = lockUsage.find((void *)lock, sizeof(void *));
    if(detailLockData == nullptr)  {
        detailLockData = lockUsage.insert((void*)lock, sizeof(void*), DetailLockData::newDetailLockData(SPIN));
        AllocatingStatus::recordANewLock(SPIN);
    }


    AllocatingStatus::initForWritingOneLockData(SPIN, detailLockData);

    if(*lock != 1) {
        AllocatingStatus::recordALockContention();
    }

    uint64_t timeStart = rdtscp();
    int result = RealX::pthread_spin_lock(lock);
    uint64_t timeStop = rdtscp();

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::recordLockCallAndCycles(1, timeStop-timeStart);
    AllocatingStatus::checkAndStartRecordingACriticalSection();

    return result;
}

int pthread_spin_trylock(pthread_spinlock_t *lock) {
    if (AllocatingStatus::outsideTrackedAllocation()|| !AllocatingStatus::sampledForCountingEvent) {
        if (!realInitialized) RealX::initializer();
        return RealX::pthread_spin_trylock(lock);
    }

    DetailLockData * detailLockData = lockUsage.find((void *)lock, sizeof(void *));
    if(detailLockData == nullptr)  {
        detailLockData = lockUsage.insert((void*)lock, sizeof(void*), DetailLockData::newDetailLockData(SPINTRY));
        AllocatingStatus::recordANewLock(SPINTRY);
    }

    AllocatingStatus::initForWritingOneLockData(SPINTRY, detailLockData);

    uint64_t timeStart = rdtscp();
    int result = RealX::pthread_spin_trylock(lock);
    uint64_t timeStop = rdtscp();

    if(result != 0) {
        AllocatingStatus::recordALockContention();
    } else {
        AllocatingStatus::checkAndStartRecordingACriticalSection();
    }

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::recordLockCallAndCycles(1, timeStop-timeStart);

    return result;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (AllocatingStatus::outsideTrackedAllocation()|| !AllocatingStatus::sampledForCountingEvent) {
        if (!realInitialized) RealX::initializer();
        return RealX::pthread_mutex_trylock(mutex);
    }

    DetailLockData * detailLockData = lockUsage.find((void *)mutex, sizeof(void *));
    if(detailLockData == nullptr)  {
        detailLockData = lockUsage.insert((void*)mutex, sizeof(void*), DetailLockData::newDetailLockData(MUTEXTRY));
        AllocatingStatus::recordANewLock(MUTEXTRY);
    }

    AllocatingStatus::initForWritingOneLockData(MUTEXTRY, detailLockData);

    uint64_t timeStart = rdtscp();
    int result = RealX::pthread_mutex_trylock(mutex);
    uint64_t timeStop = rdtscp();

    if(result != 0) {
        AllocatingStatus::recordALockContention();
    } else {
        AllocatingStatus::checkAndStartRecordingACriticalSection();
    }

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::recordLockCallAndCycles(1, timeStop-timeStart);

    return result;
}

// PTHREAD_MUTEX_UNLOCK
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if(AllocatingStatus::outsideTrackedAllocation()|| !AllocatingStatus::sampledForCountingEvent) {
        if (!realInitialized) RealX::initializer();
        return RealX::pthread_mutex_unlock(mutex);
    }

    AllocatingStatus::checkAndStopRecordingACriticalSection();
    return RealX::pthread_mutex_unlock(mutex);
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {
    if(AllocatingStatus::outsideTrackedAllocation()|| !AllocatingStatus::sampledForCountingEvent) {
        if (!realInitialized) RealX::initializer();
        return RealX::pthread_spin_unlock(lock);
    }

    AllocatingStatus::minusCycles(120);
    AllocatingStatus::checkAndStopRecordingACriticalSection();
    return RealX::pthread_spin_unlock(lock);
}
}

extern "C" {
int pthread_create(pthread_t * tid, const pthread_attr_t * attr, void *(*start_routine)(void *), void * arg) {
    if (!realInitialized) RealX::initializer();
    int result = xthreadx::thread_create(tid, attr, start_routine, arg);
    return result;
}

int pthread_join(pthread_t thread, void ** retval) {
    if (!realInitialized) RealX::initializer();
//    fprintf(stderr, "join\n");
    return xthreadx::thread_join(thread, retval);
}

void pthread_exit(void *retval) {
    if(!realInitialized) RealX::initializer();
    RealX::pthread_exit(retval);
    __builtin_unreachable();
}
} // End of extern "C"
