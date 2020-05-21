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

//spinlock improve_lock;
//Globals
uint64_t total_cycles;
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
//float memEfficiency = 0;
pid_t pid;
//size_t alignment = 0;
size_t large_object_threshold = 0;





//Array of class sizes
int num_class_sizes;
size_t* class_sizes;

MemoryUsage mu;
MemoryUsage max_mu;

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

HashMap <uint64_t, PerLockData, spinlock, PrivateHeap> lockUsage;

// pre-init private allocator memory
typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main_mallocprof;

extern "C" {
	// Function prototypes
	size_t getTotalAllocSize(size_t sz);
	void exitHandler();

	// Function aliases
	void free(void *) __attribute__ ((weak, alias("yyfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("yycalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("yymalloc")));
	void * realloc(void *, size_t) __attribute__ ((weak, alias("yyrealloc")));
	void * valloc(size_t) __attribute__ ((weak, alias("yyvalloc")));
	void * mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) __attribute__ ((weak, alias("yymmap")));
	void * memalign(size_t, size_t) __attribute__ ((weak, alias("yymemalign")));
	void * pvalloc(size_t) __attribute__ ((weak, alias("yypvalloc")));
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
	setMainThreadContention();

	ProgramStatus::setProfilerInitializedTrue();
}

__attribute__((destructor)) void finalizer_mallocprof () {}

void dumpHashmaps() {


	fprintf(stderr, "lockUsage.printUtilization():\n");
  lockUsage.printUtilization();

	fprintf(stderr, "\n");
}


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

void collectAllocMetaData(allocation_metadata *metadata);


thread_local bool realing = false;
extern void divideSmallAlloc(size_t sz, bool reused);



// Memory management functions
extern "C" {
	void * yymalloc(size_t sz) {

        if(sz == 0) {
            return NULL;
        }

        // Small allocation routine designed to service malloc requests made by
        // the dlsym() function, as well as code running prior to dlsym(). Due
        // to our linkage alias which redirects malloc calls to yymalloc
        // located in this file; when dlsym calls malloc, yymalloc is called
        // instead. Without this routine, yymalloc would simply rely on
        // RealX::malloc to fulfill the request, however, RealX::malloc would not
        // yet be assigned until the dlsym call returns. This results in a
        // segmentation fault. To remedy the problem, we detect whether the RealX
        // has finished initializing; if it has not, we fulfill malloc requests
        // using a memory mapped region. Once dlsym finishes, all future malloc
        // requests will be fulfilled by RealX::malloc, which itself is a
        // reference to the real malloc routine.
        // Also, we can't call initializer() from here, because the initializer
        // itself will call malloc().
        if(ProgramStatus::profilerInitializedIsTrue() || !ProgramStatus::selfMapInitializedIsTrue()) {
            void* ptr = MyMalloc::malloc(sz);
            return ptr;
        }

        //Malloc is being called by a thread that is already in malloc
        if (inAllocation || !inRealMain) {
            void* ptr = RealX::malloc(sz);
            return ptr;
        }

        //thread_local
        inFree = false;
        now_size = sz;
        inAllocation = true;
        PMU_init_check();

		//Data we need for each allocation
		allocation_metadata allocData = init_allocation(sz, MALLOC);
		void* object;
		//Do before
		doBefore(&allocData);
		//Do allocation
		object = RealX::malloc(sz);
        //fprintf(stderr, "malloc ptr = %p, size = %d\n", object, sz);
        doAfter(&allocData);

        allocData.reused = MemoryWaste::allocUpdate(&allocData, object);
        divideSmallAlloc(allocData.size, allocData.reused);
        size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, allocData.size, false);
        incrementMemoryUsage(sz, allocData.classSize, new_touched_bytes, object);


		allocData.cycles = allocData.tsc_after;

        collectAllocMetaData(&allocData);

		// thread_local
        inAllocation = false;

        return object;

	}

	void * yycalloc(size_t nelem, size_t elsize) {

        //countEventsOutside(true);

		if((nelem * elsize) == 0) {
				return NULL;
		}

		if (ProgramStatus::profilerInitializedIsTrue()) {
			void * ptr = NULL;
			ptr = yymalloc (nelem * elsize);
            //fprintf(stderr, "calloc 0 ptr = %p, size = %d\n", ptr, nelem * elsize);
            if (ptr) memset(ptr, 0, nelem * elsize);
			return ptr;
		}

		if (inAllocation) {
            void * ptr = RealX::calloc (nelem, elsize);
            //fprintf(stderr, "calloc 1 ptr = %p, size = %d\n", ptr, nelem * elsize);
		    return ptr;
		}

		if (!inRealMain) {
            void * ptr = RealX::calloc (nelem, elsize);
            //fprintf(stderr, "calloc 2 ptr = %p, size = %d\n", ptr, nelem * elsize);
		    return ptr;
		}

//		PMU_init_check();
//
//		// thread_local
//		inFree = false;
//		now_size = nelem * elsize;
//		inAllocation = true;
//
//		// Data we need for each allocation
//		allocation_metadata allocData = init_allocation(nelem * elsize, CALLOC);
//		void* object;
//
//		// Do before
//		doBefore(&allocData);

		// Do allocationwriteAllocData
		return(RealX::calloc(nelem, elsize));
        //fprintf(stderr, "calloc ptr = %p, size = %d\n", object, nelem * elsize);
//        doAfter(&allocData);
//
//        allocData.reused = MemoryWaste::allocUpdate(&allocData, object);
//        divideSmallAlloc(allocData.size, allocData.reused);
//        size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, allocData.size, false);
//        incrementMemoryUsage(nelem * elsize, allocData.classSize, new_touched_bytes, object);
//        // Do after
//
//		allocData.cycles = allocData.tsc_after;
//
//        collectAllocMetaData(&allocData);

		//thread_local
//		inAllocation = false;

        ///countEventsOutside(false);

//		return object;
	}

	void yyfree(void * ptr) {

        //countEventsOutside(true);

		if (!realInitialized) RealX::initializer();
        if(ptr == NULL) return;
        // Determine whether the specified object came from our global memory;
        // only call RealX::free() if the object did not come from here.
        if ( !ProgramStatus::profilerInitializedIsTrue() || !inRealMain) {
            MyMalloc::free(ptr);
            return;
        }
        if (MyMalloc::inProfilerMemory(ptr)) {
            MyMalloc::free(ptr);
            return;
        }

        PMU_init_check();

        //thread_local
        inFree = true;
        inAllocation = true;

		//Data we need for each free
		allocation_metadata allocData = init_allocation(0, FREE);
		//Do before free

        MemoryWaste::freeUpdate(&allocData, ptr);
        if(allocData.size == 0) {
            RealX::free(ptr);
            inAllocation = false;
            return;
        }
        now_size = allocData.size;
        decrementMemoryUsage(allocData.size, allocData.classSize, ptr);
        ShadowMemory::updateObject(ptr, allocData.size, true);

        //Update free counters
        doBefore(&allocData);
        //Do free
        RealX::free(ptr);

		//Do after free
		doAfter(&allocData);

            if (__builtin_expect(!globalized, true)) {

                if(allocData.size < large_object_threshold) {

                    ///Freq
                    //freq_add(2, allocData.tsc_after);

                    localTAD.numFrees++;
                    localTAD.cycles_free += allocData.tsc_after;
                    localTAD.numDeallocationFaults += allocData.after.faults;
                    localTAD.numDeallocationTlbReadMisses += allocData.after.tlb_read_misses;
                    localTAD.numDeallocationTlbWriteMisses += allocData.after.tlb_write_misses;
                    localTAD.numDeallocationCacheMisses += allocData.after.cache_misses;
                    localTAD.numDeallocationInstrs += allocData.after.instructions;
                } else {

//                    freq_add(4, allocData.tsc_after);

                    localTAD.numFrees_large++;
                    localTAD.cycles_free_large += allocData.tsc_after;
                    localTAD.numDeallocationFaults_large += allocData.after.faults;
                    localTAD.numDeallocationTlbReadMisses_large += allocData.after.tlb_read_misses;
                    localTAD.numDeallocationTlbWriteMisses_large += allocData.after.tlb_write_misses;
                    localTAD.numDeallocationCacheMisses_large += allocData.after.cache_misses;
                    localTAD.numDeallocationInstrs_large += allocData.after.instructions;
                }
            } else {
                if(allocData.size < large_object_threshold) {
                    globalize_lck.lock();
                    globalTAD.numFrees++;
                    globalTAD.cycles_free += allocData.tsc_after;
                    globalTAD.numDeallocationFaults += allocData.after.faults;
                    globalTAD.numDeallocationTlbReadMisses += allocData.after.tlb_read_misses;
                    globalTAD.numDeallocationTlbWriteMisses += allocData.after.tlb_write_misses;
                    globalTAD.numDeallocationCacheMisses += allocData.after.cache_misses;
                    globalTAD.numDeallocationInstrs += allocData.after.instructions;
                    globalize_lck.unlock();
                } else {
                    globalize_lck.lock();
                    globalTAD.numFrees_large++;
                    globalTAD.cycles_free_large += allocData.tsc_after;
                    globalTAD.numDeallocationFaults_large += allocData.after.faults;
                    globalTAD.numDeallocationTlbReadMisses_large += allocData.after.tlb_read_misses;
                    globalTAD.numDeallocationTlbWriteMisses_large += allocData.after.tlb_write_misses;
                    globalTAD.numDeallocationCacheMisses_large += allocData.after.cache_misses;
                    globalTAD.numDeallocationInstrs_large += allocData.after.instructions;
                    globalize_lck.unlock();
                }
            }
        inAllocation = false;

        //countEventsOutside(false);

    }

	void * yyrealloc(void * ptr, size_t sz) {

		if (!realInitialized) RealX::initializer();
		if( ProgramStatus::profilerInitializedIsTrue() || MyMalloc::inProfilerMemory(ptr)) {
			if(ptr == NULL) {
                return yymalloc(sz);
            }
            int * metadata = (int *)ptr - 1;
            unsigned old_size = *metadata;
            if(sz <= old_size) {
                return ptr;
            }
            void * new_obj = yymalloc(sz);
            memcpy(new_obj, ptr, old_size);
            yyfree(ptr);
            //fprintf(stderr, "realloc 0 ptr = %p, new ptr = %p, size = %d\n", ptr, new_obj, sz);
            return new_obj;
		}

		if (!mapsInitialized) {
		    void * object = RealX::realloc (ptr, sz);
            //fprintf(stderr, "realloc 1 ptr = %p, new ptr = %p, size = %d\n", ptr, object, sz);
		    return object;
		}
		if (inAllocation) {
            void * object = RealX::realloc (ptr, sz);
            //fprintf(stderr, "realloc 2 ptr = %p, new ptr = %p, size = %d\n", ptr, object, sz);
            return object;
        }
		if (!inRealMain) {
            void * object = RealX::realloc (ptr, sz);
            //fprintf(stderr, "realloc 3 ptr = %p, new ptr = %p, size = %d\n", ptr, object, sz);
            return object;
        }
//		PMU_init_check();
//
//		//Data we need for each allocation
//		allocation_metadata allocData = init_allocation(sz, REALLOC);
//
//		// allocated object
//		void * object;
//
//		//thread_local
//		inFree = false;
//		now_size = sz;
//		inAllocation = true;
//
//		//Do before
//
//        if(ptr) {
//            MemoryWaste::freeUpdate(&allocData, ptr);
//            ShadowMemory::updateObject(ptr, allocData.size, true);
//            decrementMemoryUsage(allocData.size, allocData.classSize, ptr);
//        }
//
//        doBefore(&allocData);
        //Do allocation
        return(RealX::realloc(ptr, sz));
        //fprintf(stderr, "realloc ptr = %p, new ptr = %p, size = %d\n", ptr, object, sz);
        //Do after
//        doAfter(&allocData);
//        allocData.size = sz;
//        allocData.reused = MemoryWaste::allocUpdate(&allocData, object);
//        divideSmallAlloc(allocData.size, allocData.reused);
//        size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, sz, false);
//        incrementMemoryUsage(sz, allocData.classSize, new_touched_bytes, object);
//
//
//		// cyclesForRealloc = tsc_after - tsc_before;
//		allocData.cycles = allocData.tsc_after;
//
//		//Gets overhead, address usage, mmap usage
//		//analyzeAllocation(&allocData);
//        //analyzePerfInfo(&allocData);
//        ///collectAllocMetaData(&allocData);
//
//		//thread_local
//		inAllocation = false;

        ///countEventsOutside(false);

//		return object;
	}

	inline void logUnsupportedOp() {
		fprintf(stderr, "ERROR: call to unsupported memory function: %s\n",
				__FUNCTION__);
	}


	void * yyvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
	}


	int yyposix_memalign(void **memptr, size_t alignment, size_t size) {
        //countEventsOutside(true);
    if (!inRealMain) return RealX::posix_memalign(memptr, alignment, size);

//    //thread_local
//    inFree = false;
//    now_size = size;
//    inAllocation = true;
//
//		PMU_init_check();
//
//    //Data we need for each allocation
//    allocation_metadata allocData = init_allocation(size, MALLOC);
//
//    //Do allocation
//    doBefore(&allocData);
    return(RealX::posix_memalign(memptr, alignment, size));
//    doAfter(&allocData);
//    void * object = *memptr;
//    allocData.reused = MemoryWaste::allocUpdate(&allocData, object);
//        divideSmallAlloc(allocData.size, allocData.reused);
//    //Do after
//    size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, allocData.size, false);
//    incrementMemoryUsage(size, allocData.classSize, new_touched_bytes, object);
//
//        allocData.cycles = allocData.tsc_after;
//        collectAllocMetaData(&allocData);
//    // thread_local
//    inAllocation = false;
        ///countEventsOutside(false);
//    return retval;
	}


	void * yyaligned_alloc(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}


 void * yymemalign(size_t alignment, size_t size) {
     //countEventsOutside(true);
    // fprintf(stderr, "yymemalign alignment %d, size %d\n", alignment, size);
     if (!inRealMain) return RealX::memalign(alignment, size);

//     //thread_local
//     inFree = false;
//     now_size = size;
//     inAllocation = true;
//
//     PMU_init_check();
//
//     //Data we need for each allocation
//     allocation_metadata allocData = init_allocation(size, MALLOC);
//
//     //Do allocation
//     doBefore(&allocData);
     return(RealX::memalign(alignment, size));
//     doAfter(&allocData);
//     allocData.reused = MemoryWaste::allocUpdate(&allocData, object);
//     divideSmallAlloc(allocData.size, allocData.reused);
//     //Do after
//    size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, allocData.size, false);
//    incrementMemoryUsage(size, allocData.classSize, new_touched_bytes, object);
//
//     allocData.cycles = allocData.tsc_after;
//     ///collectAllocMetaData(&allocData);
//    // thread_local
//    inAllocation = false;
//     countEventsOutside(false);
//    return object;
	}


	void * yypvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
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

void getClassSizeForStyles(void* uintaddr, allocation_metadata * allocData) {

    if(allocData->size == the_old_size) {
        allocData->classSize = the_old_classSize;
        allocData->classSizeIndex = the_old_classSizeIndex;
        return;
    }

    the_old_size = allocData->size;

        if(allocData->size > large_object_threshold) {
            allocData->classSize = allocData->size;
            allocData->classSizeIndex = num_class_sizes -1;

            the_old_classSize = allocData->classSize;
            the_old_classSizeIndex = allocData->classSizeIndex;

            return;
        }
        if(bibop) {
            for (int i = 0; i < num_class_sizes; i++) {
                size_t tempSize = class_sizes[i];
                if (allocData->size <= tempSize) {
                    allocData->classSize = tempSize;
                    allocData->classSizeIndex = i;

                    the_old_classSize = allocData->classSize;
                    the_old_classSizeIndex = allocData->classSizeIndex;

                    return;
                }
            }
        } else {
            if(allocData->size <= 24) {
                allocData->classSizeIndex = 0;
                allocData->classSize = 24;
            } else {
                allocData->classSizeIndex = (allocData->size - 24) / 16 + 1;
                allocData->classSize = class_sizes[allocData->classSizeIndex];
            }
         }
    the_old_classSize = allocData->classSize;
    the_old_classSizeIndex = allocData->classSizeIndex;
}

allocation_metadata init_allocation(size_t sz, enum memAllocType type) {
	PerfReadInfo empty;
	allocation_metadata new_metadata = {
		reused : false,
		tid: thrData.tid,
		before : empty,
		after : empty,
		size : sz,
		//classSize : getClassSizeFor(sz),
		classSize : 0,
		//classSizeIndex : getClassSizeIndex(getClassSizeForStyles(sz)),
		classSizeIndex : 0,
		cycles : 0,
//		address : 0,
		tsc_before : 0,
		tsc_after : 0,
		type : type,
		tad : NULL
	};
	return new_metadata;
}

void globalizeTAD() {

    if (__builtin_expect(globalized, false)) {
        fprintf(stderr, "The thread %lld has been globalized!\n", gettid());
    }
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
    total_cycles = globalTAD.numOutsideCycles + globalTAD.cycles_alloc + globalTAD.cycles_allocFFL + globalTAD.cycles_free +
            globalTAD.cycles_alloc_large + globalTAD.cycles_free_large;
    fprintf (ProgramStatus::outputFile, "total cycles\t\t\t\t%20lu\n", total_cycles);

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
				PerLockData * data = lock.getValue();
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


void doBefore (allocation_metadata *metadata) {
    //fprintf(stderr, "Dobefore, %d, %d\n", metadata->size, metadata->type);
	getPerfCounts(&(metadata->before));
	metadata->tsc_before = rdtscp();
    realing = true;
}

//extern void remove_mmprof_counting(allocation_metadata *metadata);

void doAfter (allocation_metadata *metadata) {
    //fprintf(stderr, "Doafter, %d, %d\n", metadata->size, metadata->type);
    realing = false;
    metadata->tsc_after = rdtscp();
	getPerfCounts(&(metadata->after));

	metadata->tsc_after -= metadata->tsc_before;

//    fprintf(stderr, "0 metadata->tsc_after = %llu\n", metadata->tsc_after);


    metadata->after.faults -= metadata->before.faults;
	metadata->after.tlb_read_misses -= metadata->before.tlb_read_misses;
	metadata->after.tlb_write_misses -= metadata->before.tlb_write_misses;
	metadata->after.cache_misses -= metadata->before.cache_misses;
	metadata->after.instructions -= metadata->before.instructions;

//    remove_mmprof_counting(metadata);

    if(metadata->tsc_after > 10000000000) {
//	    fprintf(stderr, "metadata->tsc_after = %llu\n", metadata->tsc_after);
        metadata->tsc_after = 0;
    }

    if(metadata->after.faults > 10000000000) {
//        fprintf(stderr, "metadata->after.faults = %llu\n", metadata->after.faults);
        metadata->after.faults = 0;
    }
    if(metadata->after.tlb_read_misses > 10000000000) {
//        fprintf(stderr, "metadata->after.tlb_read_misses = %llu\n", metadata->after.tlb_read_misses);
        metadata->after.tlb_read_misses = 0;
    }
    if(metadata->after.tlb_write_misses > 10000000000) {
//        fprintf(stderr, "metadata->after.tlb_write_misses = %llu\n", metadata->after.tlb_write_misses);
        metadata->after.tlb_write_misses = 0;
    }
    if(metadata->after.cache_misses > 10000000000) {
//        fprintf(stderr, "metadata->after.cache_misses = %llu\n", metadata->after.cache_misses);
        metadata->after.cache_misses = 0;
    }
    if(metadata->after.instructions > 10000000000) {
//        fprintf(stderr, "metadata->after.instructions = %llu\n", metadata->after.instructions);
        metadata->after.instructions = 0;
    }
}

void incrementGlobalMemoryAllocation(size_t size) {
  __atomic_add_fetch(&mu.realMemoryUsage, size, __ATOMIC_RELAXED);
}

void decrementGlobalMemoryAllocation(size_t size) {
  __atomic_sub_fetch(&mu.realMemoryUsage, size, __ATOMIC_RELAXED);
}

void checkGlobalRealMemoryUsage() {
    if(mu.realMemoryUsage > max_mu.realMemoryUsage) {
        max_mu.realMemoryUsage = mu.realMemoryUsage;
    }
}


void checkGlobalTotalMemoryUsage() {
    if(max_mu.totalMemoryUsage >= 100*ONE_MEGABYTE) {
        if (mu.totalMemoryUsage > max_mu.totalMemoryUsage + 10*ONE_MEGABYTE) {
            max_mu.totalMemoryUsage = mu.totalMemoryUsage;
            MemoryWaste::recordMemory(mu.realMemoryUsage, max_mu.totalMemoryUsage);
        }
    } else {
        if (mu.totalMemoryUsage > max_mu.totalMemoryUsage + ONE_MEGABYTE) {
            max_mu.totalMemoryUsage = mu.totalMemoryUsage;
            MemoryWaste::recordMemory(mu.realMemoryUsage, max_mu.totalMemoryUsage);
        }
    }

}

void incrementMemoryUsage(size_t size, size_t classSize, size_t new_touched_bytes, void * object) {


    if(classSize-size > PAGESIZE) {
        classSize -= (classSize-size)/PAGESIZE*PAGESIZE;
    }

    threadContention->realMemoryUsage += size;
    threadContention->realAllocatedMemoryUsage += classSize;
    if(new_touched_bytes > 0) {
        threadContention->totalMemoryUsage += new_touched_bytes;
    }

    if(threadContention->realMemoryUsage > threadContention->maxRealMemoryUsage) {
        threadContention->maxRealMemoryUsage = threadContention->realMemoryUsage;
    }
    if(threadContention->realAllocatedMemoryUsage > threadContention->maxRealAllocatedMemoryUsage) {
            threadContention->maxRealAllocatedMemoryUsage = threadContention->realAllocatedMemoryUsage;
        }
    if(threadContention->totalMemoryUsage > threadContention->maxTotalMemoryUsage) {
        threadContention->maxTotalMemoryUsage = threadContention->totalMemoryUsage;
    }


    incrementGlobalMemoryAllocation(size);
    if(new_touched_bytes > 0) {
        __atomic_add_fetch(&mu.totalMemoryUsage, new_touched_bytes, __ATOMIC_RELAXED);
    }
    checkGlobalRealMemoryUsage();
            checkGlobalTotalMemoryUsage();

}

void decrementMemoryUsage(size_t size, size_t classSize, void * addr) {

  if(addr == NULL) return;



    if(classSize-size > PAGESIZE) {
        classSize -= (classSize-size)/PAGESIZE*PAGESIZE;
    }

    threadContention->realMemoryUsage -= size;
    threadContention->realAllocatedMemoryUsage -= classSize;

  decrementGlobalMemoryAllocation(size);

}


void collectAllocMetaData(allocation_metadata *metadata) {
    if (__builtin_expect(!globalized, true)) {
//        cycles_without_improve[thrData.tid] += metadata->cycles;
        ///Jin
        if (!metadata->reused) {

            if(metadata->size < large_object_threshold) {

                ///Freq
//                freq_add(0, metadata->cycles);

                localTAD.cycles_alloc += metadata->cycles;
                localTAD.numAllocs++;
                localTAD.numAllocationFaults += metadata->after.faults;
                localTAD.numAllocationTlbReadMisses += metadata->after.tlb_read_misses;
                localTAD.numAllocationTlbWriteMisses += metadata->after.tlb_write_misses;
                localTAD.numAllocationCacheMisses += metadata->after.cache_misses;
                localTAD.numAllocationInstrs += metadata->after.instructions;
            } else {

                ///Freq
//                freq_add(3, metadata->cycles);

                    localTAD.cycles_alloc_large += metadata->cycles;
                    localTAD.numAllocs_large++;
                    localTAD.numAllocationFaults_large += metadata->after.faults;
                    localTAD.numAllocationTlbReadMisses_large += metadata->after.tlb_read_misses;
                    localTAD.numAllocationTlbWriteMisses_large += metadata->after.tlb_write_misses;
                    localTAD.numAllocationCacheMisses_large += metadata->after.cache_misses;
                    localTAD.numAllocationInstrs_large += metadata->after.instructions;
                }
            } else {

                if(metadata->size < large_object_threshold) {

                    ///Freq
//                    freq_add(1, metadata->cycles);

                    localTAD.cycles_allocFFL += metadata->cycles;
                    localTAD.numAllocsFFL++;
                    localTAD.numAllocationFaultsFFL += metadata->after.faults;
                    localTAD.numAllocationTlbReadMissesFFL += metadata->after.tlb_read_misses;
                    localTAD.numAllocationTlbWriteMissesFFL += metadata->after.tlb_write_misses;
                    localTAD.numAllocationCacheMissesFFL += metadata->after.cache_misses;
                    localTAD.numAllocationInstrsFFL += metadata->after.instructions;
                } else {

                    ///Freq
//                    freq_add(3, metadata->cycles);

                    localTAD.cycles_alloc_large += metadata->cycles;
                    localTAD.numAllocs_large++;
                    localTAD.numAllocationFaults_large += metadata->after.faults;
                    localTAD.numAllocationTlbReadMisses_large += metadata->after.tlb_read_misses;
                    localTAD.numAllocationTlbWriteMisses_large += metadata->after.tlb_write_misses;
                    localTAD.numAllocationCacheMisses_large += metadata->after.cache_misses;
                    localTAD.numAllocationInstrs_large += metadata->after.instructions;
                }
            }
        } else {
            globalize_lck.lock();
            if (!metadata->reused) {
                if(metadata->size < large_object_threshold) {
                    globalTAD.cycles_alloc += metadata->cycles;
                    globalTAD.numAllocs++;
                    globalTAD.numAllocationFaults += metadata->after.faults;
                    globalTAD.numAllocationTlbReadMisses += metadata->after.tlb_read_misses;
                    globalTAD.numAllocationTlbWriteMisses += metadata->after.tlb_write_misses;
                    globalTAD.numAllocationCacheMisses += metadata->after.cache_misses;
                    globalTAD.numAllocationInstrs += metadata->after.instructions;
                } else {
                    globalTAD.cycles_alloc_large += metadata->cycles;
                    globalTAD.numAllocs_large++;
                    globalTAD.numAllocationFaults_large += metadata->after.faults;
                    globalTAD.numAllocationTlbReadMisses_large += metadata->after.tlb_read_misses;
                    globalTAD.numAllocationTlbWriteMisses_large += metadata->after.tlb_write_misses;
                    globalTAD.numAllocationCacheMisses_large += metadata->after.cache_misses;
                    globalTAD.numAllocationInstrs_large += metadata->after.instructions;
                }
            } else {
                if(metadata->size < large_object_threshold) {
                    globalTAD.cycles_allocFFL += metadata->cycles;
                    globalTAD.numAllocsFFL++;
                    globalTAD.numAllocationFaultsFFL += metadata->after.faults;
                    globalTAD.numAllocationTlbReadMissesFFL += metadata->after.tlb_read_misses;
                    globalTAD.numAllocationTlbWriteMissesFFL += metadata->after.tlb_write_misses;
                    globalTAD.numAllocationCacheMissesFFL += metadata->after.cache_misses;
                    globalTAD.numAllocationInstrsFFL += metadata->after.instructions;
                } else {
                    globalTAD.cycles_alloc_large += metadata->cycles;
                    globalTAD.numAllocs_large++;
                    globalTAD.numAllocationFaults_large += metadata->after.faults;
                    globalTAD.numAllocationTlbReadMisses_large += metadata->after.tlb_read_misses;
                    globalTAD.numAllocationTlbWriteMisses_large += metadata->after.tlb_write_misses;
                    globalTAD.numAllocationCacheMisses_large += metadata->after.cache_misses;
                    globalTAD.numAllocationInstrs_large += metadata->after.instructions;
                }
            }
            globalize_lck.unlock();
        }
}
//
//void analyzePerfInfo(allocation_metadata *metadata) {
//	collectAllocMetaData(metadata);
//
///*
//	// DEBUG BLOCK
//	if((metadata->after.instructions - metadata->before.instructions) != 0) {
//		fprintf(stderr, "Malloc from thread       %d\n"
//				"From free list:          %s\n"
//				"Num faults:              %ld\n"
//				"Num TLB read misses:     %ld\n"
//				"Num TLB write misses:    %ld\n"
//				"Num cache misses:        %ld\n"
//				"Num instructions:        %ld\n\n",
//				metadata->tid, metadata->reused ? "true" : "false",
//				metadata->after.faults - metadata->before.faults,
//				metadata->after.tlb_read_misses - metadata->before.tlb_read_misses,
//				metadata->after.tlb_write_misses - metadata->before.tlb_write_misses,
//				metadata->after.cache_misses - metadata->before.cache_misses,
//				metadata->after.instructions - metadata->before.instructions);
//	}
//*/
//}

pid_t gettid() {
    return syscall(__NR_gettid);
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
