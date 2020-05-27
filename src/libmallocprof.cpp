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

bool bibop = false;
bool bumpPointer = false;
bool isLibc = false;
bool inRealMain = false;
bool mapsInitialized = false;
bool opening_maps_file = false;
bool realInitialized = false;

bool inConstructor = false;

char smaps_fileName[30];

extern char * program_invocation_name;
pid_t pid;


/// REQUIRED!
thread_local thread_data thrData;
//thread_local bool waiting;
thread_local bool inAllocation;
thread_local bool inFree;
thread_local size_t now_size;
//thread_local bool inDeallocation;
thread_local bool inMmap;
thread_local bool PMUinit = false;
thread_local uint64_t myThreadID;
thread_local thread_alloc_data localTAD;
thread_local bool globalized = false;
thread_alloc_data globalTAD;

//thread_local PerfAppFriendly friendliness;

thread_local size_t the_old_size;
thread_local size_t the_old_classSize;
thread_local short the_old_classSizeIndex;

spinlock globalize_lck;

friendly_data globalFriendlyData;

HashMap <void *, DetailLockData, spinlock, PrivateHeap> lockUsage;

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
    ThreadLocalStatus::getRunningThreadIndex();

	ProgramStatus::setProfilerInitializedTrue();
}

__attribute__((destructor)) void finalizer_mallocprof () {}

//extern void improve_cycles_stage_count();

void exitHandler() {
    //countEventsOutside(true);
    inRealMain = false;
    //improve_cycles_stage_count(-1);
	#ifndef NO_PMU
	stopSampling();

	//doPerfCounterRead();
	#endif

	#warning Disabled smaps functionality (timer, file handle cleanup)

	globalizeTAD();
	writeAllocData();

//	 Calculate and print the application friendliness numbers.
	calcAppFriendliness();

			fflush(ProgramStatus::outputFile);
		fclose(ProgramStatus::outputFile);

}

// MallocProf's main function
int libmallocprof_main(int argc, char ** argv, char ** envp) {
	// Register our cleanup routine as an on-exit handler.
	atexit(exitHandler);

	PMU_init_check();
	lockUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 128*32);
  MemoryWaste::initialize();
	mapsInitialized = true;

	inRealMain = true;
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


void globalizeTAD() {

    globalize_lck.lock();

    globalTAD.numAllocationFaults += localTAD.numAllocationFaults;
	globalTAD.numAllocationTlbReadMisses += localTAD.numAllocationTlbReadMisses;
	globalTAD.numAllocationTlbWriteMisses += localTAD.numAllocationTlbWriteMisses;
	globalTAD.numAllocationCacheMisses += localTAD.numAllocationCacheMisses;
	globalTAD.numAllocationInstrs += localTAD.numAllocationInstrs;

    globalTAD.numAllocationFaults_large += localTAD.numAllocationFaults_large;
    globalTAD.numAllocationTlbReadMisses_large += localTAD.numAllocationTlbReadMisses_large;
    globalTAD.numAllocationTlbWriteMisses_large += localTAD.numAllocationTlbWriteMisses_large;
    globalTAD.numAllocationCacheMisses_large += localTAD.numAllocationCacheMisses_large;
    globalTAD.numAllocationInstrs_large += localTAD.numAllocationInstrs_large;

	globalTAD.numAllocationFaultsFFL += localTAD.numAllocationFaultsFFL;
	globalTAD.numAllocationTlbReadMissesFFL += localTAD.numAllocationTlbReadMissesFFL;
	globalTAD.numAllocationTlbWriteMissesFFL += localTAD.numAllocationTlbWriteMissesFFL;
	globalTAD.numAllocationCacheMissesFFL += localTAD.numAllocationCacheMissesFFL;
	globalTAD.numAllocationInstrsFFL += localTAD.numAllocationInstrsFFL;

	globalTAD.numDeallocationFaults += localTAD.numDeallocationFaults;
	globalTAD.numDeallocationCacheMisses += localTAD.numDeallocationCacheMisses;
	globalTAD.numDeallocationInstrs += localTAD.numDeallocationInstrs;
	globalTAD.numDeallocationTlbReadMisses += localTAD.numDeallocationTlbReadMisses;
	globalTAD.numDeallocationTlbWriteMisses += localTAD.numDeallocationTlbWriteMisses;

    globalTAD.numDeallocationFaults_large += localTAD.numDeallocationFaults_large;
    globalTAD.numDeallocationCacheMisses_large += localTAD.numDeallocationCacheMisses_large;
    globalTAD.numDeallocationInstrs_large += localTAD.numDeallocationInstrs_large;
    globalTAD.numDeallocationTlbReadMisses_large += localTAD.numDeallocationTlbReadMisses_large;
    globalTAD.numDeallocationTlbWriteMisses_large += localTAD.numDeallocationTlbWriteMisses_large;

	for(int i = 0; i < LOCK_TYPE_TOTAL; ++i) {
        globalTAD.lock_nums[i] += localTAD.lock_nums[i];
    }

	globalTAD.cycles_alloc += localTAD.cycles_alloc;
    globalTAD.cycles_alloc_large += localTAD.cycles_alloc_large;
	globalTAD.cycles_allocFFL += localTAD.cycles_allocFFL;
	globalTAD.cycles_free += localTAD.cycles_free;
    globalTAD.cycles_free_large += localTAD.cycles_free_large;
	globalTAD.numAllocs += localTAD.numAllocs;
    globalTAD.numAllocs_large += localTAD.numAllocs_large;
	globalTAD.numAllocsFFL += localTAD.numAllocsFFL;
	globalTAD.numFrees += localTAD.numFrees;
    globalTAD.numFrees_large += localTAD.numFrees_large;

	globalTAD.numOutsideCacheMisses += localTAD.numOutsideCacheMisses;
	globalTAD.numOutsideFaults += localTAD.numOutsideFaults;
	globalTAD.numOutsideTlbReadMisses += localTAD.numOutsideTlbReadMisses;
	globalTAD.numOutsideTlbWriteMisses += localTAD.numOutsideTlbWriteMisses;
	globalTAD.numOutsideCycles += localTAD.numOutsideCycles;

	globalize_lck.unlock();

    globalized = true;
}

void writeAllocData () {
	fprintf(ProgramStatus::outputFile, ">>> large_object_threshold\t%20zu\n", large_object_threshold);
	writeThreadMaps();
	writeThreadContention();
	fflush (ProgramStatus::outputFile);
}

uint64_t total_lock_cycles;
uint64_t total_lock_calls;

void writeThreadMaps () {
///Here

	double numAllocs = safeDivisor(globalTAD.numAllocs);
	double numAllocsFFL = safeDivisor(globalTAD.numAllocsFFL);
	double numFrees = safeDivisor(globalTAD.numFrees);
	double numAllocs_large = safeDivisor(globalTAD.numAllocs_large);
    double numFrees_large = safeDivisor(globalTAD.numFrees_large);

  fprintf (ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>    ALLOCATION NUM    <<<<<<<<<<<<<<<\n");
	fprintf (ProgramStatus::outputFile, "small new allocations\t\t\t\t%20lu\n", globalTAD.numAllocs);
	fprintf (ProgramStatus::outputFile, "small reused allocations\t\t%20lu\n", globalTAD.numAllocsFFL);
	fprintf (ProgramStatus::outputFile, "small deallocations\t\t\t\t\t%20lu\n", globalTAD.numFrees);
    fprintf (ProgramStatus::outputFile, "large allocations\t\t\t\t\t\t%20lu\n", globalTAD.numAllocs_large);
    fprintf (ProgramStatus::outputFile, "large deallocations\t\t\t\t\t%20lu\n", globalTAD.numFrees_large);
	uint64_t leak;
	if(globalTAD.numAllocs+globalTAD.numAllocsFFL+globalTAD.numAllocs_large > globalTAD.numFrees+globalTAD.numFrees_large) {
	    leak = (globalTAD.numAllocs+globalTAD.numAllocsFFL+globalTAD.numAllocs_large) - (globalTAD.numFrees+globalTAD.numFrees_large);
	} else {
	    leak = 0;
	}
    fprintf (ProgramStatus::outputFile, "potential leak num\t\t\t\t\t%20lu\n", leak);
	fprintf (ProgramStatus::outputFile, "\n");

  fprintf (ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>    SMALL NEW ALLOCATIONS (%lu)   <<<<<<<<<<<<<<<\n", globalTAD.numAllocs);
	fprintf (ProgramStatus::outputFile, "cycles\t\t\t\t\t\t\t\t\t\t\t%20lu(%3d%%)\tavg = %0.1lf\n", globalTAD.cycles_alloc, globalTAD.cycles_alloc/(total_cycles/100), (globalTAD.cycles_alloc / numAllocs));
	fprintf (ProgramStatus::outputFile, "faults\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationFaults, (globalTAD.numAllocationFaults*100 / numAllocs));
	fprintf (ProgramStatus::outputFile, "tlb read misses\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbReadMisses, (globalTAD.numAllocationTlbReadMisses*100 / numAllocs));
	fprintf (ProgramStatus::outputFile, "tlb write misses\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbWriteMisses, (globalTAD.numAllocationTlbWriteMisses*100 / numAllocs));
	fprintf (ProgramStatus::outputFile, "cache misses\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationCacheMisses, (globalTAD.numAllocationCacheMisses / numAllocs));
	fprintf (ProgramStatus::outputFile, "instructions\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationInstrs, (globalTAD.numAllocationInstrs / numAllocs));
	fprintf (ProgramStatus::outputFile, "\n");

	fprintf (ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>    SMALL FREELIST ALLOCATIONS (%lu)  <<<<<<<<<<<<<<<\n", globalTAD.numAllocsFFL);
	fprintf (ProgramStatus::outputFile, "cycles\t\t\t\t\t\t\t\t\t\t\t%20lu(%3d%%)\tavg = %0.1lf\n", globalTAD.cycles_allocFFL, globalTAD.cycles_allocFFL/(total_cycles/100), (globalTAD.cycles_allocFFL / numAllocsFFL));
	fprintf (ProgramStatus::outputFile, "faults\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationFaultsFFL, (globalTAD.numAllocationFaultsFFL*100 / numAllocsFFL));
	fprintf (ProgramStatus::outputFile, "tlb read misses\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbReadMissesFFL, (globalTAD.numAllocationTlbReadMissesFFL*100 / numAllocsFFL));
	fprintf (ProgramStatus::outputFile, "tlb write misses\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbWriteMissesFFL, (globalTAD.numAllocationTlbWriteMissesFFL*100 / numAllocsFFL));
	fprintf (ProgramStatus::outputFile, "cache misses\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationCacheMissesFFL, (globalTAD.numAllocationCacheMissesFFL / numAllocsFFL));
	fprintf (ProgramStatus::outputFile, "instructions\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationInstrsFFL, (globalTAD.numAllocationInstrsFFL / numAllocsFFL));
	fprintf (ProgramStatus::outputFile, "\n");

	fprintf (ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>    SMALL DEALLOCATIONS (%lu)   <<<<<<<<<<<<<<<\n", globalTAD.numFrees);
	fprintf (ProgramStatus::outputFile, "cycles\t\t\t\t\t\t\t\t\t\t\t%20lu(%3d%%)\tavg = %0.1lf\n", globalTAD.cycles_free, globalTAD.cycles_free/(total_cycles/100), (globalTAD.cycles_free / numFrees));
	fprintf (ProgramStatus::outputFile, "faults\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationFaults, (globalTAD.numDeallocationFaults*100 / numFrees));
	fprintf (ProgramStatus::outputFile, "tlb read misses\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationTlbReadMisses, (globalTAD.numDeallocationTlbReadMisses*100 / numFrees));
	fprintf (ProgramStatus::outputFile, "tlb write misses\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationTlbWriteMisses, (globalTAD.numDeallocationTlbWriteMisses*100 / numFrees));
	fprintf (ProgramStatus::outputFile, "cache misses\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numDeallocationCacheMisses, (globalTAD.numDeallocationCacheMisses / numFrees));
	fprintf (ProgramStatus::outputFile, "instrctions\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numDeallocationInstrs, (globalTAD.numDeallocationInstrs / numFrees));

    fprintf (ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>    LARGE ALLOCATIONS (%lu)   <<<<<<<<<<<<<<<\n", globalTAD.numAllocs_large);
    fprintf (ProgramStatus::outputFile, "cycles\t\t\t\t\t\t\t\t\t\t\t%20lu(%3d%%)\tavg = %0.1lf\n", globalTAD.cycles_alloc_large, globalTAD.cycles_alloc_large/(total_cycles/100), (globalTAD.cycles_alloc_large / numAllocs_large));
    fprintf (ProgramStatus::outputFile, "faults\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationFaults_large, (globalTAD.numAllocationFaults_large*100 / numAllocs_large));
    fprintf (ProgramStatus::outputFile, "tlb read misses\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbReadMisses_large, (globalTAD.numAllocationTlbReadMisses_large*100 / numAllocs_large));
    fprintf (ProgramStatus::outputFile, "tlb write misses\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbWriteMisses_large, (globalTAD.numAllocationTlbWriteMisses_large*100 / numAllocs_large));
    fprintf (ProgramStatus::outputFile, "cache misses\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationCacheMisses_large, (globalTAD.numAllocationCacheMisses_large / numAllocs_large));
    fprintf (ProgramStatus::outputFile, "instructions\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationInstrs_large, (globalTAD.numAllocationInstrs_large / numAllocs_large));
    fprintf (ProgramStatus::outputFile, "\n");

    fprintf (ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>    LARGE DEALLOCATIONS (%lu)   <<<<<<<<<<<<<<<\n", globalTAD.numFrees_large);
    fprintf (ProgramStatus::outputFile, "cycles\t\t\t\t\t\t\t\t\t\t\t%20lu(%3d%%)\tavg = %0.1lf\n", globalTAD.cycles_free_large, globalTAD.cycles_free_large/(total_cycles/100), (globalTAD.cycles_free_large / numFrees_large));
    fprintf (ProgramStatus::outputFile, "faults\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationFaults_large, (globalTAD.numDeallocationFaults_large*100 / numFrees_large));
    fprintf (ProgramStatus::outputFile, "tlb read misses\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationTlbReadMisses_large, (globalTAD.numDeallocationTlbReadMisses_large*100 / numFrees_large));
    fprintf (ProgramStatus::outputFile, "tlb write misses\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationTlbWriteMisses_large, (globalTAD.numDeallocationTlbWriteMisses_large*100 / numFrees_large));
    fprintf (ProgramStatus::outputFile, "cache misses\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numDeallocationCacheMisses_large, (globalTAD.numDeallocationCacheMisses_large / numFrees_large));
    fprintf (ProgramStatus::outputFile, "instrctions\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numDeallocationInstrs_large, (globalTAD.numDeallocationInstrs_large / numFrees_large));

    ThreadContention globalizedThreadContention;
    for(int i = 0; i <= threadcontention_index; i++) {
        ThreadContention* data = &all_threadcontention_array[i];
        for(int j = LOCK_TYPE_MUTEX; j < LOCK_TYPE_TOTAL; j++) {
            for(int k = 1; k < 4; ++k) {
                globalizedThreadContention.pmdata[j].calls[k] += data->pmdata[j].calls[k];
                globalizedThreadContention.pmdata[j].cycles[k] += data->pmdata[j].cycles[k];
            }
            globalizedThreadContention.pmdata[j].new_calls += data->pmdata[j].new_calls;
            globalizedThreadContention.pmdata[j].new_cycles += data->pmdata[j].new_cycles;
            globalizedThreadContention.pmdata[j].ffl_calls += data->pmdata[j].ffl_calls;
            globalizedThreadContention.pmdata[j].ffl_cycles += data->pmdata[j].ffl_cycles;
        }
        globalizedThreadContention.critical_section_counter += data->critical_section_counter;
        globalizedThreadContention.critical_section_duration += data->critical_section_duration;
    }

    for(int j = LOCK_TYPE_MUTEX; j < LOCK_TYPE_TOTAL; j++) {
        for(int k = 1; k < 4; ++k) {
            total_lock_calls += globalizedThreadContention.pmdata[j].calls[k];
            total_lock_cycles += globalizedThreadContention.pmdata[j].cycles[k];
        }
        total_lock_calls += globalizedThreadContention.pmdata[j].new_calls;
        total_lock_calls += globalizedThreadContention.pmdata[j].ffl_calls;
        total_lock_cycles += globalizedThreadContention.pmdata[j].new_cycles;
        total_lock_cycles += globalizedThreadContention.pmdata[j].ffl_cycles;
    }



    fprintf (ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>     LOCK TOTALS     <<<<<<<<<<<<<<<\n");
    fprintf (ProgramStatus::outputFile, "total_lock_calls\t\t\t\t\t%20u\n", total_lock_calls);
    if(total_lock_calls > 0) {
        fprintf (ProgramStatus::outputFile, "total_lock_cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", total_lock_cycles,
                 total_lock_cycles / (total_cycles / 100 ), (double)total_lock_cycles / safeDivisor(total_lock_calls));

        fprintf (ProgramStatus::outputFile, "\npthread mutex locks\t\t\t\t\t%20u\n", globalTAD.lock_nums[0]);
        if(globalTAD.lock_nums[0] > 0) {
            fprintf (ProgramStatus::outputFile, "small new alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[0].new_calls);
            fprintf (ProgramStatus::outputFile, "small new alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[0].new_calls / safeDivisor(globalTAD.numAllocs));
            fprintf (ProgramStatus::outputFile, "small new alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[0].new_cycles,
                     globalizedThreadContention.pmdata[0].new_cycles / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[0].new_cycles / safeDivisor(globalizedThreadContention.pmdata[0].new_calls));

            fprintf (ProgramStatus::outputFile, "small reused alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[0].ffl_calls);
            fprintf (ProgramStatus::outputFile, "small reused alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[0].ffl_calls / safeDivisor(globalTAD.numAllocsFFL));
            fprintf (ProgramStatus::outputFile, "small reused alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[0].ffl_cycles,
                     globalizedThreadContention.pmdata[0].ffl_cycles / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[0].ffl_cycles / safeDivisor(globalizedThreadContention.pmdata[0].ffl_calls));

            fprintf (ProgramStatus::outputFile, "large alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[0].calls[1]);
            fprintf (ProgramStatus::outputFile, "large alloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[0].calls[1] / safeDivisor(globalTAD.numAllocs_large));
            fprintf (ProgramStatus::outputFile, "large alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[0].cycles[1],
                     globalizedThreadContention.pmdata[0].cycles[1] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[0].cycles[1] / safeDivisor(globalizedThreadContention.pmdata[0].calls[1]));

            fprintf (ProgramStatus::outputFile, "small dealloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[0].calls[2]);
            fprintf (ProgramStatus::outputFile, "small dealloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[0].calls[2] / safeDivisor(globalTAD.numFrees));
            fprintf (ProgramStatus::outputFile, "small dealloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[0].cycles[2],
                     globalizedThreadContention.pmdata[0].cycles[2] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[0].cycles[2] / safeDivisor(globalizedThreadContention.pmdata[0].calls[2]));

            fprintf (ProgramStatus::outputFile, "large dealloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[0].calls[3]);
            fprintf (ProgramStatus::outputFile, "large dealloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[0].calls[3] / safeDivisor(globalTAD.numFrees_large));
            fprintf (ProgramStatus::outputFile, "large dealloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[0].cycles[3],
                     globalizedThreadContention.pmdata[0].cycles[3] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[0].cycles[3] / safeDivisor(globalizedThreadContention.pmdata[0].calls[3]));
        }


        fprintf (ProgramStatus::outputFile, "\npthread spin locks\t\t\t\t\t%20u\n", globalTAD.lock_nums[1]);
        if(globalTAD.lock_nums[1] > 0) {
            fprintf (ProgramStatus::outputFile, "small new alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[1].new_calls);
            fprintf (ProgramStatus::outputFile, "small new alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[1].new_calls / safeDivisor(globalTAD.numAllocs));
            fprintf (ProgramStatus::outputFile, "small new alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[1].new_cycles,
                     globalizedThreadContention.pmdata[1].new_cycles / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[1].new_cycles / safeDivisor(globalizedThreadContention.pmdata[1].new_calls));

            fprintf (ProgramStatus::outputFile, "small reused alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[1].ffl_calls);
            fprintf (ProgramStatus::outputFile, "small reused alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[1].ffl_calls / safeDivisor(globalTAD.numAllocsFFL));
            fprintf (ProgramStatus::outputFile, "small reused alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[1].ffl_cycles,
                     globalizedThreadContention.pmdata[1].ffl_cycles / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[1].ffl_cycles / safeDivisor(globalizedThreadContention.pmdata[1].ffl_calls));

            fprintf (ProgramStatus::outputFile, "large alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[1].calls[1]);
            fprintf (ProgramStatus::outputFile, "large alloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[1].calls[1] / safeDivisor(globalTAD.numAllocs_large));
            fprintf (ProgramStatus::outputFile, "large alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[1].cycles[1],
                     globalizedThreadContention.pmdata[1].cycles[1] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[1].cycles[1] / safeDivisor(globalizedThreadContention.pmdata[1].calls[1]));

            fprintf (ProgramStatus::outputFile, "small dealloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[1].calls[2]);
            fprintf (ProgramStatus::outputFile, "small dealloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[1].calls[2] / safeDivisor(globalTAD.numFrees));
            fprintf (ProgramStatus::outputFile, "small dealloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[1].cycles[2],
                     globalizedThreadContention.pmdata[1].cycles[2] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[1].cycles[2] / safeDivisor(globalizedThreadContention.pmdata[1].calls[2]));

            fprintf (ProgramStatus::outputFile, "large dealloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[1].calls[3]);
            fprintf (ProgramStatus::outputFile, "large dealloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[1].calls[3] / safeDivisor(globalTAD.numFrees_large));
            fprintf (ProgramStatus::outputFile, "large dealloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[1].cycles[3],
                     globalizedThreadContention.pmdata[1].cycles[3] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[1].cycles[3] / safeDivisor(globalizedThreadContention.pmdata[1].calls[3]));

        }

        fprintf (ProgramStatus::outputFile, "\npthread trylocks\t\t\t\t\t\t%20u\n", globalTAD.lock_nums[2]);
        if(globalTAD.lock_nums[2] > 0) {
            fprintf (ProgramStatus::outputFile, "small new alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[2].new_calls);
            fprintf (ProgramStatus::outputFile, "small new alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[2].new_calls / safeDivisor(globalTAD.numAllocs));
            fprintf (ProgramStatus::outputFile, "small new alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[2].new_cycles,
                     globalizedThreadContention.pmdata[2].new_cycles / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[2].new_cycles / safeDivisor(globalizedThreadContention.pmdata[2].new_calls));

            fprintf (ProgramStatus::outputFile, "small reused alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[2].ffl_calls);
            fprintf (ProgramStatus::outputFile, "small reused alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[2].ffl_calls / safeDivisor(globalTAD.numAllocsFFL));
            fprintf (ProgramStatus::outputFile, "small reused alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[2].ffl_cycles,
                     globalizedThreadContention.pmdata[2].ffl_cycles / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[2].ffl_cycles / safeDivisor(globalizedThreadContention.pmdata[2].ffl_calls));

            fprintf (ProgramStatus::outputFile, "large alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[2].calls[1]);
            fprintf (ProgramStatus::outputFile, "large alloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[2].calls[1] / safeDivisor(globalTAD.numAllocs_large));
            fprintf (ProgramStatus::outputFile, "large alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[2].cycles[1],
                     globalizedThreadContention.pmdata[2].cycles[1] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[2].cycles[1] / safeDivisor(globalizedThreadContention.pmdata[2].calls[1]));

            fprintf (ProgramStatus::outputFile, "small dealloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[2].calls[2]);
            fprintf (ProgramStatus::outputFile, "small dealloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[2].calls[2] / safeDivisor(globalTAD.numFrees));
            fprintf (ProgramStatus::outputFile, "small dealloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[2].cycles[2],
                     globalizedThreadContention.pmdata[2].cycles[2] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[2].cycles[2] / safeDivisor(globalizedThreadContention.pmdata[2].calls[2]));

            fprintf (ProgramStatus::outputFile, "large dealloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[2].calls[3]);
            fprintf (ProgramStatus::outputFile, "large dealloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[2].calls[3] / safeDivisor(globalTAD.numFrees_large));
            fprintf (ProgramStatus::outputFile, "large dealloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[2].cycles[3],
                     globalizedThreadContention.pmdata[2].cycles[3] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[2].cycles[3] / safeDivisor(globalizedThreadContention.pmdata[2].calls[3]));
        }


        fprintf (ProgramStatus::outputFile, "\npthread spin trylocks\t\t\t\t%20u\n", globalTAD.lock_nums[3]);
        if(globalTAD.lock_nums[3] > 0) {
            fprintf (ProgramStatus::outputFile, "small new alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[3].new_calls);
            fprintf (ProgramStatus::outputFile, "small new alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[3].new_calls / safeDivisor(globalTAD.numAllocs));
            fprintf (ProgramStatus::outputFile, "small new alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[3].new_cycles,
                     globalizedThreadContention.pmdata[3].new_cycles / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[3].new_cycles / safeDivisor(globalizedThreadContention.pmdata[3].new_calls));

            fprintf (ProgramStatus::outputFile, "small reused alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[3].ffl_calls);
            fprintf (ProgramStatus::outputFile, "small reused alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[3].ffl_calls / safeDivisor(globalTAD.numAllocsFFL));
            fprintf (ProgramStatus::outputFile, "small reused alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[3].ffl_cycles,
                     globalizedThreadContention.pmdata[3].ffl_cycles / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[3].ffl_cycles / safeDivisor(globalizedThreadContention.pmdata[3].ffl_calls));

            fprintf (ProgramStatus::outputFile, "large alloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[3].calls[1]);
            fprintf (ProgramStatus::outputFile, "large alloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[3].calls[1] / safeDivisor(globalTAD.numAllocs_large));
            fprintf (ProgramStatus::outputFile, "large alloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[3].cycles[1],
                     globalizedThreadContention.pmdata[3].cycles[1] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[3].cycles[1] / safeDivisor(globalizedThreadContention.pmdata[3].calls[1]));

            fprintf (ProgramStatus::outputFile, "small dealloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[3].calls[2]);
            fprintf (ProgramStatus::outputFile, "small dealloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[3].calls[2] / safeDivisor(globalTAD.numFrees));
            fprintf (ProgramStatus::outputFile, "small dealloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[3].cycles[2],
                     globalizedThreadContention.pmdata[3].cycles[2] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[3].cycles[2] / safeDivisor(globalizedThreadContention.pmdata[3].calls[2]));

            fprintf (ProgramStatus::outputFile, "large dealloc calls\t\t\t\t\t\t\t\t\t\t\t\t%20u\n", globalizedThreadContention.pmdata[3].calls[3]);
            fprintf (ProgramStatus::outputFile, "large dealloc alloc calls per alloc\t\t\t\t\t\t\t\t\t\t\t\t%10.1f\n", (double)globalizedThreadContention.pmdata[3].calls[3] / safeDivisor(globalTAD.numFrees_large));
            fprintf (ProgramStatus::outputFile, "large dealloc cycles\t\t\t\t\t%20lu(%3d%%)\tavg =%10.1f\n\n", globalizedThreadContention.pmdata[3].cycles[3],
                     globalizedThreadContention.pmdata[3].cycles[3] / (total_cycles/100),
                     (double)globalizedThreadContention.pmdata[3].cycles[3] / safeDivisor(globalizedThreadContention.pmdata[3].calls[3]));
    }
        fprintf (ProgramStatus::outputFile, ">>>\n critical_section\t\t\t\t%20lu\n",
                 globalizedThreadContention.critical_section_counter);
        fprintf (ProgramStatus::outputFile, ">>> critical_section_cycles\t\t%18lu(%3d%%)\tavg = %.1f\n",
                 globalizedThreadContention.critical_section_duration,
                 globalizedThreadContention.critical_section_duration / (total_cycles/100),
                 ((double)globalizedThreadContention.critical_section_duration / safeDivisor(globalizedThreadContention.critical_section_counter)));
        writeContention();
}
}

void writeContention () {

		fprintf(ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>>>>>>>>>>> DETAILED LOCK USAGE <<<<<<<<<<<<<<<<<<<<<<<<<\n");
		for(auto lock : lockUsage) {
            DetailLockData * data = lock.getValue();
				if(data->contendCalls*100/data->calls >= 2 || data->cycles/data->calls >= (double)total_lock_cycles / safeDivisor(total_lock_calls)) {
                    fprintf(ProgramStatus::outputFile, "lockAddr = %#lx\t\ttype = %s\t\tmax contention thread = %5d\t\t",
                            lock.getKey(), LockTypeToString(data->type), data->maxContendThreads);
                    fprintf(ProgramStatus::outputFile, "invocations = %10u\t\tcontention times = %10u\t\tcontention rate = %3u%%\t\t", data->calls, data->contendCalls, data->contendCalls*100/data->calls);
                    fprintf(ProgramStatus::outputFile, "cycles = %20lu(%3d%%)\n", data->cycles, data->cycles/(total_cycles/100));
                    fprintf(ProgramStatus::outputFile, "avg cycles = %10u\n", data->cycles/data->calls);
                    fprintf(ProgramStatus::outputFile, "small alloc calls = %10lu\t\tsmall alloc cycles = %20lu(%3d%%)\tavg=%10.1f\n", data->percalls[0],
                            data->percycles[0], data->percycles[0]/(total_cycles/100),
                            (double)data->percycles[0]/safeDivisor(data->percalls[0]));
                    fprintf(ProgramStatus::outputFile, "large alloc calls = %10lu\t\tlarge alloc cycles = %20lu(%3d%%)\tavg=%10.1f\n", data->percalls[1],
                            data->percycles[1], data->percycles[1]/(total_cycles/100),
                            (double)data->percycles[1]/safeDivisor(data->percalls[1]));
                    fprintf(ProgramStatus::outputFile, "small dealloc calls = %10lu\t\tsmall dealloc cycles = %20lu(%3d%%)\tavg=%10.1f\n", data->percalls[2],
                            data->percycles[2], data->percycles[2]/(total_cycles/100),
                            (double)data->percycles[2]/safeDivisor(data->percalls[2]));
                    fprintf(ProgramStatus::outputFile, "large dealloc calls = %10lu\t\tlarge dealloc cycles = %20lu(%3d%%)\tavg=%10.1f\n\n", data->percalls[3],
                            data->percycles[3], data->percycles[3]/(total_cycles/100),
                            (double)data->percycles[3]/safeDivisor(data->percalls[3]));
                }
		}
        fprintf(ProgramStatus::outputFile, "\n");
		//fflush(ProgramStatus::outputFile);
}

void updateGlobalFriendlinessData() {
		friendly_data * thrFriendlyData = &thrData.friendlyData;

		__atomic_add_fetch(&globalFriendlyData.numAccesses, thrFriendlyData->numAccesses, __ATOMIC_SEQ_CST);
		__atomic_add_fetch(&globalFriendlyData.numCacheWrites, thrFriendlyData->numCacheWrites, __ATOMIC_SEQ_CST);
		__atomic_add_fetch(&globalFriendlyData.numCacheBytes, thrFriendlyData->numCacheBytes, __ATOMIC_SEQ_CST);
		__atomic_add_fetch(&globalFriendlyData.numPageBytes, thrFriendlyData->numPageBytes, __ATOMIC_SEQ_CST);

    __atomic_add_fetch(&globalFriendlyData.numObjectFS, thrFriendlyData->numObjectFS, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&globalFriendlyData.numActiveFS, thrFriendlyData->numActiveFS, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&globalFriendlyData.numPassiveFS, thrFriendlyData->numPassiveFS, __ATOMIC_SEQ_CST);

            __atomic_add_fetch(&globalFriendlyData.numObjectFSCacheLine, thrFriendlyData->numObjectFSCacheLine, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&globalFriendlyData.numActiveFSCacheLine, thrFriendlyData->numActiveFSCacheLine, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&globalFriendlyData.numPassiveFSCacheLine, thrFriendlyData->numPassiveFSCacheLine, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&globalFriendlyData.numPassiveFSCacheLine, thrFriendlyData->numPassiveFSCacheLine, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&globalFriendlyData.cachelines, thrFriendlyData->cachelines, __ATOMIC_SEQ_CST);

		#ifdef THREAD_OUTPUT			// DEBUG BLOCK
		if(ProgramStatus::outputFile) {
				pid_t tid = thrData.tid;
				double avgCacheUtil = (double) thrFriendlyData->numCacheBytes / (thrFriendlyData->numAccesses * CACHELINE_SIZE);
				double avgPageUtil = (double) thrFriendlyData->numPageBytes / (thrFriendlyData->numAccesses * PAGESIZE);
				FILE * outfd = ProgramStatus::outputFile;
				//FILE * outfd = stderr;
				fprintf(outfd, "tid %d : num sampled accesses = %ld\n", tid, thrFriendlyData->numAccesses);
				fprintf(outfd, "tid %d : total cache bytes accessed = %ld\n", tid, thrFriendlyData->numCacheBytes);
				fprintf(outfd, "tid %d : total page bytes accessed  = %ld\n", tid, thrFriendlyData->numPageBytes);
				fprintf(outfd, "tid %d : num cache line writes      = %ld\n", tid, thrFriendlyData->numCacheWrites);
				//fprintf(outfd, "tid %d : num cache owner conflicts  = %ld\n", tid, thrFriendlyData->numCacheOwnerConflicts);
				fprintf(outfd, "tid %d : avg. cache util = %0.4f\n", tid, avgCacheUtil);
				fprintf(outfd, "tid %d : avg. page util  = %0.4f\n", tid, avgPageUtil);
		}
		#endif // END DEBUG BLOCK
}

extern long totalMem;

void calcAppFriendliness() {

    //freq_printout();

    // Final call to update the global data, using this (the main thread's) local data.

    fprintf(ProgramStatus::outputFile, "\n>>>>>>>>>>>>>>>>>>>>>>>>> APP FRIENDLINESS <<<<<<<<<<<<<<<<<<<<<<<<<\n");

		updateGlobalFriendlinessData();

		unsigned long totalAccesses = __atomic_load_n(&globalFriendlyData.numAccesses, __ATOMIC_SEQ_CST);
		unsigned long totalCacheWrites = __atomic_load_n(&globalFriendlyData.numCacheWrites, __ATOMIC_SEQ_CST);
		unsigned long totalCacheBytes = __atomic_load_n(&globalFriendlyData.numCacheBytes, __ATOMIC_SEQ_CST);
		unsigned long totalPageBytes = __atomic_load_n(&globalFriendlyData.numPageBytes, __ATOMIC_SEQ_CST);

		unsigned long totalObjectFS = __atomic_load_n(&globalFriendlyData.numObjectFS, __ATOMIC_SEQ_CST);
		unsigned long totalActiveFS = __atomic_load_n(&globalFriendlyData.numActiveFS, __ATOMIC_SEQ_CST);
		unsigned long totalPassiveFS = __atomic_load_n(&globalFriendlyData.numPassiveFS, __ATOMIC_SEQ_CST);

    unsigned long totalObjectFSCacheLine = __atomic_load_n(&globalFriendlyData.numObjectFSCacheLine, __ATOMIC_SEQ_CST);
    unsigned long totalActiveFSCacheLine = __atomic_load_n(&globalFriendlyData.numActiveFSCacheLine, __ATOMIC_SEQ_CST);
    unsigned long totalPassiveFSCacheLine = __atomic_load_n(&globalFriendlyData.numPassiveFSCacheLine, __ATOMIC_SEQ_CST);
    unsigned long totalCacheLine = __atomic_load_n(&globalFriendlyData.cachelines, __ATOMIC_SEQ_CST);

		double avgTotalCacheUtil =
		        (double) totalCacheBytes / (totalAccesses * CACHELINE_SIZE);
		double avgTotalPageUtil =
		        (double) totalPageBytes / (totalAccesses * PAGESIZE);
		FILE * outfd = ProgramStatus::outputFile;
		//FILE * outfd = stderr;
		fprintf(outfd, "sampled accesses\t\t\t\t\t\t\t\t\t\t\t=%20ld\n", totalAccesses);
		if(totalAccesses > 0) {
            fprintf(outfd, "storing instructions\t\t\t\t\t\t\t\t\t=%20ld\n", totalCacheWrites);

            fprintf(outfd, "cycles outside allocs\t=%16lu(%3d%%)\n", globalTAD.numOutsideCycles, globalTAD.numOutsideCycles/(total_cycles/100));
            fprintf(outfd, "cache misses outside allocs per 1M cycles\t=%15.1lf\n", (double)globalTAD.numOutsideCacheMisses/safeDivisor(globalTAD.numOutsideCycles/1000000));
            fprintf(outfd, "page faults outside allocs per 1M cycles\t=%15.1lf\n", (double)globalTAD.numOutsideFaults/safeDivisor(globalTAD.numOutsideCycles/1000000 ));
            fprintf(outfd, "TLB read misses outside allocs per 1M cycles\t=%15.1lf\n", (double)globalTAD.numOutsideTlbReadMisses/safeDivisor(globalTAD.numOutsideCycles/1000000 ));
            fprintf(outfd, "TLB write misses outside allocs per 1M cycles\t=%15.1lf\n", (double)globalTAD.numOutsideTlbWriteMisses/safeDivisor(globalTAD.numOutsideCycles/1000000 ));

            fprintf(outfd, "object false sharing accesses\t=%15ld(%3d%%)\n", totalObjectFS, (int)((double)totalObjectFS/safeDivisor(totalAccesses/100)));
            fprintf(outfd, "active false sharing accesses\t=%15ld(%3d%%)\n", totalActiveFS, (int)((double)totalActiveFS/safeDivisor(totalAccesses/100)));
            fprintf(outfd, "passive false sharing accesses\t=%15ld(%3d%%)\n", totalPassiveFS, (int)((double)totalPassiveFS/safeDivisor(totalAccesses/100)));

            fprintf(outfd, "object false sharing cache lines\t=%15ld(%3d%%)\n", totalObjectFSCacheLine, (int)((double)totalObjectFSCacheLine/safeDivisor(totalCacheLine/100)));
            fprintf(outfd, "active false sharing cache lines\t=%15ld(%3d%%)\n", totalActiveFSCacheLine, (int)((double)totalActiveFSCacheLine/safeDivisor(totalCacheLine/100)));
            fprintf(outfd, "passive false sharing cache lines\t=%15ld(%3d%%)\n", totalPassiveFSCacheLine, (int)((double)totalPassiveFSCacheLine/safeDivisor(totalCacheLine/100)));

            fprintf(outfd, "avg. cache utilization\t\t\t\t\t\t\t\t=%19d%%\n", (int)(avgTotalCacheUtil * 100));
            fprintf(outfd, "avg. page utilization\t\t\t\t\t\t\t\t\t=%19d%%\n", (int)(avgTotalPageUtil * 100));
		}

}

const char * LockTypeToString(LockType type) {
		switch(type) {
				case LOCK_TYPE_MUTEX:
					return "mutex";

				case LOCK_TYPE_SPINLOCK:
					return "spinlock";

				case LOCK_TYPE_TRYLOCK:
					return "trylock";

				case LOCK_TYPE_SPIN_TRYLOCK:
					return "spin_trylock";

				default:
					return "unknown";
		}
}
