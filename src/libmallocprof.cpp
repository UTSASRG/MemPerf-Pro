/**
 * @file libmallocprof.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 * @author Sam Silvestro <sam.silvestro@utsa.edu>
 * @author Richard Salcedo <kbgagt@gmail.com>
 * @author Stefen Ramirez <stfnrmz0@gmail.com>
 */

//#include <atomic>  //atomic vars
#include <dlfcn.h> //dlsym
#include <fcntl.h> //fopen flags
#include <stdio.h> //print, getline
#include <signal.h>
#include <time.h>
#include <new>
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "libmallocprof.h"
#include "real.hh"
#include "selfmap.hh"
#include "spinlock.hh"
#include "xthreadx.hh"
#include "recordscale.hh"
#include "memwaste.h"
#include <sched.h>
#include <stdlib.h>
#include "programstatus.h"
#include "mymalloc.h"
#include "allocatingstatus.h"
#include "threadlocalstatus.h"

thread_local bool PMUinit = false;

HashMap <void *, DetailLockData, spinlock, PrivateHeap> lockUsage;
pid_t pid;

// pre-init private allocator memory
typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main_mallocprof;

extern "C" {
	// Function prototypes
	void exitHandler();

	// Function aliases
	void free(void *) __attribute__ ((weak, alias("yyfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("yycalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("yymalloc")));
	void * realloc(void *, size_t) __attribute__ ((weak, alias("yyrealloc")));
	void * mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) __attribute__ ((weak, alias("yymmap")));
	void * memalign(size_t, size_t) __attribute__ ((weak, alias("yymemalign")));
	int posix_memalign(void **, size_t, size_t) __attribute__ ((weak,
				alias("yyposix_memalign")));
}

 inline void PMU_init_check() {
    #ifndef NO_PMU
    // If the PMU sampler has not yet been set up for this thread, set it up now
    if(__builtin_expect(!PMUinit, false)) {
      initPMU();
      PMUinit = true;
    }
    #endif
 }

// Constructor
__attribute__((constructor)) void initializer() {
    ProgramStatus::checkSystemIs64Bits();
    RealX::initializer();
    ShadowMemory::initialize();
    ProgramStatus::initIO();
    ThreadLocalStatus::getARunningThreadIndex();

	ProgramStatus::setProfilerInitializedTrue();
}

__attribute__((destructor)) void finalizer_mallocprof () {}

//extern void improve_cycles_stage_count();

void exitHandler() {
	#ifndef NO_PMU
	stopSampling();
	#endif

	#warning Disabled smaps functionality (timer, file handle cleanup)

    GlobalStatus::globalize();
    GlobalStatus::printOutput();


}

// MallocProf's main function
int libmallocprof_main(int argc, char ** argv, char ** envp) {
	// Register our cleanup routine as an on-exit handler.
	atexit(exitHandler);

	PMU_init_check();
	lockUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 128*32);
  MemoryWaste::initialize();

	int result = real_main_mallocprof (argc, argv, envp);
	return result;
}

extern "C" int __libc_start_main(main_fn_t, int, char **, void (*)(),
		void (*)(), void (*)(), void *) __attribute__((weak,
			alias("libmallocprof_libc_start_main")));

extern "C" int libmallocprof_libc_start_main(main_fn_t main_fn, int argc,
		char ** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(),
		void * stack_end) {
	auto real_libc_start_main =
		(decltype(__libc_start_main) *)dlsym(RTLD_NEXT, "__libc_start_main");
	real_main_mallocprof = main_fn;
	return real_libc_start_main(libmallocprof_main, argc, argv, init, fini,
			rtld_fini, stack_end);
}


// Memory management functions
extern "C" {
	void * yymalloc(size_t sz) {

        if(sz == 0) {
            return NULL;
        }

        if(AllocatingStatus::profilerNotInitialized()) {
            return MyMalloc::malloc(sz);
        }


        AllocatingStatus::updateAllocatingStatusBeforeRealFunction(MALLOC, sz);
		void * object = RealX::malloc(sz);
        AllocatingStatus::updateAllocatingStatusAfterRealFunction();
        AllocatingStatus::updateAllocatingInfoToThreadLocalData();
        return object;

	}


	void * yycalloc(size_t nelem, size_t elsize) {

		if((nelem * elsize) == 0) {
				return NULL;
		}

		if (AllocatingStatus::profilerNotInitialized()) {
            return MyMalloc::malloc(sz);
		}

        AllocatingStatus::updateAllocatingStatusBeforeRealFunction(CALLOC, sz);
        void * object = RealX::calloc(nelem, elsize));
        AllocatingStatus::updateAllocatingStatusAfterRealFunction();
        AllocatingStatus::updateAllocatingInfoToThreadLocalData();
		return object;
	}


	void yyfree(void * ptr) {

        if(ptr == NULL) return;

        if (MyMalloc::inProfilerMemory(ptr)) {
            return MyMalloc::free(ptr);
        }

        AllocatingStatus::updateFreeingStatusBeforeRealFunction(FREE, ptr);
        RealX::free(ptr);
        AllocatingStatus::updateFreeingStatusAfterRealFunction();
        AllocatingStatus::updateAllocatingInfoToThreadLocalData();
    }


	void * yyrealloc(void * ptr, size_t sz) {

		if(MyMalloc::inProfilerMemory(ptr)) {
            MyMalloc::free(ptr);
            return MyMalloc::malloc(sz);
		}

        AllocatingStatus::updateFreeingStatusBeforeRealFunction(REALLOC, ptr);
        AllocatingStatus::updateAllocatingStatusBeforeRealFunction(REALLOC, sz);
        void * object = RealX::realloc(ptr, sz);
        AllocatingStatus::updateAllocatingStatusAfterRealFunction(object);
        AllocatingStatus::updateAllocatingInfoToThreadLocalData();
        return object;
	}


	int yyposix_memalign(void **memptr, size_t alignment, size_t size) {
        if(size == 0) {
            return NULL;
        }

        if(AllocatingStatus::profilerNotInitialized()) {
            return MyMalloc::malloc(sz);
        }


        AllocatingStatus::updateAllocatingStatusBeforeRealFunction(POSIX_MEMALIGN, size);
        int retval = RealX::posix_memalign(memptr, alignment, size);
        AllocatingStatus::updateAllocatingStatusAfterRealFunction(*memptr);
        AllocatingStatus::updateAllocatingInfoToThreadLocalData();
        return retval;

	}


 void * yymemalign(size_t alignment, size_t size) {
     if(size == 0) {
         return NULL;
     }

     if(AllocatingStatus::profilerNotInitialized()) {
         return MyMalloc::malloc(size);
     }


     AllocatingStatus::updateAllocatingStatusBeforeRealFunction(MEMALIGN, sz);
     void * object = RealX::memalign(alignment, size);
     AllocatingStatus::updateAllocatingStatusAfterRealFunction(object);
     AllocatingStatus::updateAllocatingInfoToThreadLocalData();
     return object;
	}



	// PTHREAD_CREATE
	int pthread_create(pthread_t * tid, const pthread_attr_t * attr,
			void *(*start_routine)(void *), void * arg) {
		if (!realInitialized) RealX::initializer();

		int result = xthreadx::thread_create(tid, attr, start_routine, arg);
		return result;
	}

	// PTHREAD_JOIN
	int pthread_join(pthread_t thread, void **retval) {
		if (!realInitialized) RealX::initializer();

		int result = RealX::pthread_join (thread, retval);
		return result;
	}

	// PTHREAD_EXIT
	void pthread_exit(void *retval) {
		if(!realInitialized) {
				RealX::initializer();
		}

		xthreadx::threadExit();
        RealX::pthread_exit(retval);

        // We should no longer be here, as pthread_exit is marked [[noreturn]]
        __builtin_unreachable();
	}
} // End of extern "C"

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
