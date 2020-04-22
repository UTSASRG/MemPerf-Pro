/**
 * @file libmallocprof.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 * @author Sam Silvestro <sam.silvestro@utsa.edu>
 * @author Richard Salcedo <kbgagt@gmail.com>
 * @author Stefen Ramirez <stfnrmz0@gmail.com>
 */

#include <atomic>  //atomic vars
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


//Globals
bool bibop = false;
bool bumpPointer = false;
bool isLibc = false;
bool inRealMain = false;
bool mapsInitialized = false;
bool opening_maps_file = false;
bool realInitialized = false;
bool selfmapInitialized = false;
//std::atomic<bool> creatingThread (false);
bool inConstructor = false;
char* allocator_name;
char* allocatorFileName;
char smaps_fileName[30];
//extern char data_start;
//extern char _etext;
extern char * program_invocation_name;
extern void * __libc_stack_end;
//float memEfficiency = 0;
initStatus profilerInitialized = NOT_INITIALIZED;
pid_t pid;
//size_t alignment = 0;
size_t large_object_threshold = 0;
//size_t metadata_object = 0;
//size_t metadata_overhead = 0;
//size_t total_blowup = 0;
//size_t totalSizeAlloc = 0;
//size_t totalSizeFree = 0;
//size_t totalSizeDiff = 0;
//size_t totalMemOverhead = 0;

//Smaps Sampling-------------//
//void setupSignalHandler();
//void startSignalTimer();
size_t smaps_bufferSize = 1024;
//FILE* smaps_infile = nullptr;
char* smaps_buffer;
//OverheadSample worstCaseOverhead;
//OverheadSample currentCaseOverhead;
//SMapEntry* smapsDontCheck[100];
//short smapsDontCheckIndex = 0;
//short smapsDontCheckNumEntries = 0;
//timer_t smap_timer;
//uint64_t smap_samples = 0;
//uint64_t smap_sample_cycles = 0;
struct itimerspec stopTimer;
struct itimerspec resumeTimer;
//unsigned timer_nsec = 333000000;
unsigned timer_nsec = 0;
//unsigned timer_sec = 0;
unsigned timer_sec = 1;
//Smaps Sampling-------------//

//Array of class sizes
int num_class_sizes;
size_t* class_sizes;

//Atomic Globals ATOMIC
//std::atomic_bool mmap_active(false);
//std::atomic_bool sbrk_active(false);
//std::atomic_bool madvise_active(false);
//std::atomic<std::size_t> freed_bytes (0);
//std::atomic<std::size_t> blowup_bytes (0);
//std::atomic<std::size_t> alignment_bytes (0);
//
//thread_local uint64_t total_time_wait = 0;

//uint num_sbrk = 0;
//uint num_madvise = 0;
//uint malloc_mmaps = 0;
//std::size_t size_sbrk = 0;

//thread_local uint blowup_allocations = 0;
thread_local unsigned long long total_cycles_start = 0;
std::atomic<std::uint64_t> total_global_cycles (0);
//std::atomic<std::uint64_t> cycles_alloc (0);
//std::atomic<std::uint64_t> cycles_allocFFL (0);
//std::atomic<std::uint64_t> cycles_free (0);

//uint64_t * realMemoryUsageBySizes = nullptr;
//uint64_t * memoryUsageBySizes = nullptr;
//uint64_t * freedMemoryUsageBySizes = nullptr;
//uint64_t * realMemoryUsageBySizesWhenMax = nullptr;
//uint64_t * memoryUsageBySizesWhenMax = nullptr;
//uint64_t * freedMemoryUsageBySizesWhenMax = nullptr;
MemoryUsage mu;
MemoryUsage max_mu;

/// REQUIRED!
//std::atomic<unsigned>* globalFreeArray = nullptr;
//uint64_t * globalNumAllocsBySizes = nullptr;
//uint64_t* globalNumAllocsFFLBySizes = nullptr;
//Thread local variables THREAD_LOCAL
thread_local thread_data thrData;
//thread_local bool waiting;
thread_local bool inAllocation;
thread_local bool inFree;
//thread_local bool inDeallocation;
thread_local bool inMmap;
thread_local bool PMUinit = false;
//thread_local uint64_t timeAttempted;
//thread_local uint64_t timeWaiting;
//thread_local unsigned* localFreeArray = nullptr;
//thread_local bool localFreeArrayInitialized = false;
//thread_local uint64_t * localNumAllocsBySizes = nullptr;
//thread_local bool localNumAllocsBySizesInitialized = false;
//thread_local uint64_t * localNumAllocsFFLBySizes = nullptr;
//thread_local bool localNumAllocsFFLBySizesInitialized = false;
thread_local uint64_t myThreadID;
thread_local thread_alloc_data localTAD;
thread_local bool globalized = false;
thread_alloc_data globalTAD;

thread_local void* myLocalMem;
thread_local void* myLocalMemEnd;
thread_local uint64_t myLocalPosition;
thread_local uint64_t myLocalAllocations;
thread_local bool myLocalMemInitialized = false;
//thread_local PerfAppFriendly friendliness;

thread_local size_t the_old_size;
thread_local size_t the_old_classSize;
thread_local short the_old_classSizeIndex;

spinlock globalize_lck;

// Pre-init private allocator memory
char myMem[TEMP_MEM_SIZE];
void* myMemEnd;
uint64_t myMemPosition = 0;
uint64_t myMemAllocations = 0;
spinlock myMemLock;

char* myMem_hash;
void* myMemEnd_hash;
uint64_t myMemPosition_hash = 0;
uint64_t myMemAllocations_hash = 0;
spinlock myMemLock_hash;
unsigned long long HASH_SIZE = (unsigned long long)1024*1024*1024*2;

friendly_data globalFriendlyData;

HashMap <uint64_t, PerLockData, spinlock, PrivateHeap> lockUsage;

// pre-init private allocator memory
char myBuffer[TEMP_MEM_SIZE];
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
	void * aligned_alloc(size_t, size_t) __attribute__ ((weak,
				alias("yyaligned_alloc")));
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
__attribute__((constructor)) initStatus initializer() {

	if(profilerInitialized == INITIALIZED){
			return profilerInitialized;
	}
	
	profilerInitialized = IN_PROGRESS;

	inConstructor = true;
	// Ensure we are operating on a system using 64-bit pointers.
	// This is necessary, as later we'll be taking the low 8-byte word
	// of callsites. This could obviously be expanded to support 32-bit systems
	// as well, in the future.
	size_t ptrSize = sizeof(void *);
	if(ptrSize != EIGHT_BYTES) {
		fprintf(stderr, "error: unsupported pointer size: %zu\n", ptrSize);
		abort();
    }
    myMemEnd = (void*) (myMem + TEMP_MEM_SIZE);
    myMemLock.init();

    globalize_lck.init();
    pid = getpid();

    // Allocate and initialize all shadow memory-related mappings
    ShadowMemory::initialize();

    RealX::initializer();

    myMem_hash = (char*)RealX::mmap(NULL, HASH_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    myMemEnd_hash = (void*) (myMem_hash + HASH_SIZE);
    //fprintf(stderr, "myMen_hash = %p, %p\n", myMem_hash, myMemEnd_hash);
    myMemLock_hash.init();


	void * program_break = RealX::sbrk(0);
	thrData.tid = syscall(__NR_gettid);

	thrData.stackStart = (char *)__builtin_frame_address(0);
	thrData.stackEnd = (char *)__libc_stack_end;

	setThreadContention();

	allocator_name = (char*) myMalloc(100);
	selfmap::getInstance().getTextRegions();
	selfmapInitialized = true;

	//Build the name of the .info file
	//This file should be located in the same directory
	//as the allocator. readAllocatorFile() will open the file

	allocatorFileName = (char*)myMalloc(128);
	char* end_path = strrchr(allocator_name, '/');
	char* end_name = strchr(end_path, '.') - 1;
	char* start_name = end_path + 1;
	uint64_t name_bytes = (uint64_t)end_name - (uint64_t)end_path;
	uint64_t path_bytes = (uint64_t)start_name - (uint64_t)allocator_name;
	short ext_bytes = 6;
	void* r;

	r = memcpy(allocatorFileName, allocator_name, path_bytes);
	if (r != allocatorFileName) {perror("memcpy fail?"); abort();}
	r = memcpy((allocatorFileName+path_bytes), start_name, name_bytes);
	if (r != (allocatorFileName+path_bytes)) {perror("memcpy fail?"); abort();}
	snprintf ((allocatorFileName+path_bytes+name_bytes), ext_bytes, ".info");
    fprintf(stderr, "name: %s\n", allocatorFileName);
	readAllocatorFile();

	// Generate the name of our output file, then open it for writing.
	char outputFile[MAX_FILENAME_LEN];
//	snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmallocprof_%d_main_thread.txt",
//			program_invocation_name, pid);
    snprintf(outputFile, MAX_FILENAME_LEN, "/home/jinzhou/parsec/records/%s_libmallocprof_%d_main_thread.txt",
             program_invocation_name, pid);
    fprintf(stderr, "%s\n", outputFile);
	// Will overwrite current file; change the fopen flag to "a" for append.
	thrData.output = fopen(outputFile, "w");
	if(thrData.output == NULL) {
		perror("error: unable to open output file to write");
		return INIT_ERROR;
	}

	fprintf(thrData.output, ">>> stack start @ %p, stack end @ %p\n", thrData.stackStart, thrData.stackEnd);
	fprintf(thrData.output, ">>> program break @ %p\n", program_break);
	//fflush(thrData.output);

	if (bibop) {
		//Print class sizes to thrData output
		fprintf (thrData.output, ">>> class_sizes ");

		//Create and init Overhead for each class size
		for (int i = 0; i < num_class_sizes; i++) {

			size_t cs = class_sizes[i];

			fprintf (thrData.output, "%zu ", cs);
			//overhead.insert(cs, newOverhead());
		}

		fprintf (thrData.output, "\n");
		//fflush(thrData.output);
	}
	else {
		num_class_sizes = 2;
		//Create an entry in the overhead hashmap with key 0
		//overhead.insert(BP_OVERHEAD, newOverhead());
	}

	profilerInitialized = INITIALIZED;

	smaps_buffer = (char*) myMalloc(smaps_bufferSize);
	sprintf(smaps_fileName, "/proc/%d/smaps", pid);


	resumeTimer.it_value.tv_sec = timer_sec;
	resumeTimer.it_value.tv_nsec = timer_nsec;
	resumeTimer.it_interval.tv_sec = timer_sec;
	resumeTimer.it_interval.tv_nsec = timer_nsec;

	stopTimer.it_value.tv_sec = 0;
	stopTimer.it_value.tv_nsec = 0;
	stopTimer.it_interval.tv_sec = 0;
	stopTimer.it_interval.tv_nsec = 0;

	//worstCaseOverhead.efficiency = 100.00;

	#warning Disabled smaps functionality (timer, signal handler)
	/*
	start_smaps();
	setupSignalHandler();
	startSignalTimer();*/


	total_cycles_start = rdtscp();
	inConstructor = false;
	return profilerInitialized;
}

__attribute__((destructor)) void finalizer_mallocprof () {}

void dumpHashmaps() {

	// fprintf(stderr, "addressUsage.printUtilization():\n");
	// addressUsage.printUtilization();

//	fprintf(stderr, "overhead.printUtilization():\n");
//	overhead.printUtilization();

	fprintf(stderr, "lockUsage.printUtilization():\n");
  lockUsage.printUtilization();

	//fprintf(stderr, "threadContention.printUtilization():\n");
	//threadContention.printUtilization();

	// fprintf(stderr, "threadToCSM.printUtilization():\n");
	// threadToCSM.printUtilization();
	fprintf(stderr, "\n");
}

void printMyMemUtilization () {

	fprintf(stderr, "&myMem = %p\n", myMem);
	fprintf(stderr, "myMemEnd = %p\n", myMemEnd);
	fprintf(stderr, "myMemPosition = %lu\n", myMemPosition);
	fprintf(stderr, "myMemAllocations = %lu\n", myMemAllocations);
}

void exitHandler() {

	inRealMain = false;
	unsigned long long total_cycles_end = rdtscp();
	total_global_cycles += total_cycles_end - total_cycles_start;
	#ifndef NO_PMU
	stopSampling();

	//doPerfCounterRead();
	#endif

	#warning Disabled smaps functionality (timer, file handle cleanup)
	/*
	fclose(smaps_infile);
	if (timer_settime(smap_timer, 0, &stopTimer, NULL) == -1) {
			perror("timer_settime failed");
			abort();
	}
	if (timer_delete(smap_timer) == -1) {
			perror("timer_delete failed");
	}
	*/

//	if(thrData.output) {
//			fflush(thrData.output);
//	}
	globalizeTAD();
	writeAllocData();

	// Calculate and print the application friendliness numbers.
	calcAppFriendliness();

    	if(thrData.output) {
			fflush(thrData.output);
	}

	if(thrData.output) {
		fclose(thrData.output);
	}

	#warning Disabled smaps functionality (output)
	/*
	uint64_t avg;
	if(smap_samples != 0) {
			avg = (smap_sample_cycles / smap_samples);
	}

	fprintf(stderr, "---WorstCaseOverhead---\n"
					"Samples:              %lu\n"
					"AvgCycles:            %lu\n"
					"PhysicalMem:          %lu\n"
					"Alignment:            %lu\n"
					"Blowup:               %lu\n"
					"Efficiency:           %.4f\n"
					"-----------------------\n",
					smap_samples,
					avg,
					worstCaseOverhead.kb,
					worstCaseOverhead.alignment,
					worstCaseOverhead.blowup,
					worstCaseOverhead.efficiency);
	*/
}

// MallocProf's main function
int libmallocprof_main(int argc, char ** argv, char ** envp) {
	// Register our cleanup routine as an on-exit handler.
	atexit(exitHandler);

	PMU_init_check();
	lockUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 128*32);
  MemoryWaste::initialize();
  MemoryWaste::initForNewTid();
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

//thread_local CacheMissesOutsideInfo eventOutside_before;
//thread_local CacheMissesOutsideInfo eventOutside_after;
thread_local PerfReadInfo eventOutside_before;
thread_local PerfReadInfo eventOutside_after;
thread_local bool eventOutsideStart = false;

void countEventsOutside(bool end) {
    if(end && !eventOutsideStart) {
        return;
    }
    eventOutsideStart = !end;
    if(!end) {
        getPerfCounts(&eventOutside_before);
    } else {
        getPerfCounts(&eventOutside_after);
    }
    if(end) {
        eventOutside_after.cache_misses -= eventOutside_before.cache_misses;
        if(eventOutside_after.cache_misses > 10000000000) {
            eventOutside_after.cache_misses = 0;
        }
        localTAD.numOutsideCacheMisses += eventOutside_after.cache_misses;
    }
}

// Memory management functions
extern "C" {
	void * yymalloc(size_t sz) {

        countEventsOutside(true);

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
        if((profilerInitialized != INITIALIZED) || (!selfmapInitialized)) {
            void* ptr = myMalloc (sz);
            //fprintf(stderr, "0 ptr = %p, size = %d\n", ptr, sz);
            return ptr;
            //return myMalloc (sz);
        }

        //Malloc is being called by a thread that is already in malloc
        if (inAllocation) {
            void* ptr = RealX::malloc(sz);
            //fprintf(stderr, "1 ptr = %p, size = %d\n", ptr, sz);
            return ptr;
        }

        if (!inRealMain) {
            void* ptr = RealX::malloc(sz);
            //fprintf(stderr, "2 ptr = %p, size = %d\n", ptr, sz);
            return ptr;
        }
        //thread_local
        inFree = false;
        inAllocation = true;
        PMU_init_check();

		//Data we need for each allocation
		allocation_metadata allocData = init_allocation(sz, MALLOC);
		void* object;
		//Do before
		doBefore(&allocData);
		//Do allocation
//        fprintf(stderr, "malloc %d\n", sz);
		object = RealX::malloc(sz);
        doAfter(&allocData);
        //fprintf(stderr, "malloc done %d %p\n", sz, object);
//        allocData.address = (uint64_t) object;
        allocData.reused = MemoryWaste::allocUpdate(&allocData, object);
        size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, allocData.size, false);
        incrementMemoryUsage(sz, allocData.classSize, new_touched_bytes, object);
        //Do after
        //fprintf(stderr, "*** malloc(%zu) -> %p\n", sz, object);

		allocData.cycles = allocData.tsc_after;

		// Gets overhead, address usage, mmap usage, memHWM, and prefInfo
        collectAllocMetaData(&allocData);

		//fprintf(stderr, "ptr = %p, size = %d\n\n", object, sz);

		// thread_local
        inAllocation = false;

        countEventsOutside(false);

        return object;

	}

	void * yycalloc(size_t nelem, size_t elsize) {

        countEventsOutside(true);

		if((nelem * elsize) == 0) {
				return NULL;
		}

		if (profilerInitialized != INITIALIZED) {

			void * ptr = NULL;
			ptr = yymalloc (nelem * elsize);
			if (ptr) memset(ptr, 0, nelem * elsize);
			return ptr;
		}

		if (inAllocation) return RealX::calloc(nelem, elsize);

		if (!inRealMain) return RealX::calloc (nelem, elsize);

		PMU_init_check();

		// thread_local
		inFree = false;
		inAllocation = true;

		// Data we need for each allocation
		allocation_metadata allocData = init_allocation(nelem * elsize, CALLOC);
		void* object;

		// Do before
		doBefore(&allocData);

		// Do allocation
		object = RealX::calloc(nelem, elsize);
        doAfter(&allocData);
        allocData.reused = MemoryWaste::allocUpdate(&allocData, object);
        size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, allocData.size, false);
        incrementMemoryUsage(nelem * elsize, allocData.classSize, new_touched_bytes, object);
        // Do after

		allocData.cycles = allocData.tsc_after;

        collectAllocMetaData(&allocData);

		//thread_local
		inAllocation = false;

        countEventsOutside(false);

		return object;
	}

	void yyfree(void * ptr) {

        countEventsOutside(true);

		if (!realInitialized) RealX::initializer();
        if(ptr == NULL) return;
        // Determine whether the specified object came from our global memory;
        // only call RealX::free() if the object did not come from here.
        if (ptr >= (void *)myMem && ptr <= myMemEnd) {
            myFree (ptr);
            return;
        }
        if (ptr >= (void *)myMem_hash && ptr <= myMemEnd_hash) {
            return;
        }
        if ((profilerInitialized != INITIALIZED) || !inRealMain) {
            myFree(ptr);
            return;
        }
        //fprintf(stderr, "free...%p\n", ptr);
        PMU_init_check();

        //thread_local
        inFree = true;
        inAllocation = true;

		//Data we need for each free
		allocation_metadata allocData = init_allocation(0, FREE);
		//Do before free

        MemoryWaste::freeUpdate(&allocData, ptr);
        decrementMemoryUsage(allocData.size, allocData.classSize, ptr);
        ShadowMemory::updateObject(ptr, allocData.size, true);

        //Update free counters
        doBefore(&allocData);
        //Do free
        RealX::free(ptr);

		//Do after free
		doAfter(&allocData);

        //thread_local
//        cycles_free += allocData.tsc_after;
        ///below
            if (__builtin_expect(!globalized, true)) {
                if(allocData.size < large_object_threshold) {
                    localTAD.numFrees++;
                    localTAD.cycles_free += allocData.tsc_after;
                    localTAD.numDeallocationFaults += allocData.after.faults;
                    localTAD.numDeallocationTlbReadMisses += allocData.after.tlb_read_misses;
                    localTAD.numDeallocationTlbWriteMisses += allocData.after.tlb_write_misses;
                    localTAD.numDeallocationCacheMisses += allocData.after.cache_misses;
                    localTAD.numDeallocationInstrs += allocData.after.instructions;
                } else {
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

        countEventsOutside(false);

    }

	void * yyrealloc(void * ptr, size_t sz) {

        countEventsOutside(true);

	    //fprintf(stderr, "yyrealloc...%p, %d\n", ptr, sz);
		if (!realInitialized) RealX::initializer();
		if((profilerInitialized != INITIALIZED) || (ptr >= (void *)myMem && ptr <= myMemEnd)) {
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
			return new_obj;
		}

		if (!mapsInitialized) return RealX::realloc (ptr, sz);
		if (inAllocation) return RealX::realloc (ptr, sz);
		if (!inRealMain) return RealX::realloc (ptr, sz);

		PMU_init_check();

		//Data we need for each allocation
		allocation_metadata allocData = init_allocation(sz, REALLOC);

		// allocated object
		void * object;

		//thread_local
		inFree = false;
		inAllocation = true;

		//Do before

        if(ptr) {
            MemoryWaste::freeUpdate(&allocData, ptr);
            ShadowMemory::updateObject(ptr, allocData.size, true);
            decrementMemoryUsage(allocData.size, allocData.classSize, ptr);
        }

        doBefore(&allocData);
        //Do allocation
        object = RealX::realloc(ptr, sz);
        //Do after
        doAfter(&allocData);
        allocData.size = sz;
        allocData.reused = MemoryWaste::allocUpdate(&allocData, object);
        size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, sz, false);
        incrementMemoryUsage(sz, allocData.classSize, new_touched_bytes, object);


		// cyclesForRealloc = tsc_after - tsc_before;
		allocData.cycles = allocData.tsc_after;

		//Gets overhead, address usage, mmap usage
		//analyzeAllocation(&allocData);
        //analyzePerfInfo(&allocData);
        collectAllocMetaData(&allocData);

		//thread_local
		inAllocation = false;

        countEventsOutside(false);

		return object;
	}

	inline void logUnsupportedOp() {
		fprintf(thrData.output,
				"ERROR: call to unsupported memory function: %s\n",
				__FUNCTION__);
	}


	void * yyvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
	}


	int yyposix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!inRealMain) return RealX::posix_memalign(memptr, alignment, size);

    //thread_local
    inFree = false;
    inAllocation = true;

		PMU_init_check();

    //Data we need for each allocation
    allocation_metadata allocData = init_allocation(size, MALLOC);

    //Do allocation
    int retval = RealX::posix_memalign(memptr, alignment, size);
    void * object = *memptr;
    allocData.reused = MemoryWaste::allocUpdate(&allocData, object);
    //Do after
    size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, allocData.size, false);
    incrementMemoryUsage(size, allocData.classSize, new_touched_bytes, object);

    // thread_local
    inAllocation = false;
    return retval;
	}


	void * yyaligned_alloc(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}


 void * yymemalign(size_t alignment, size_t size) {
    // fprintf(stderr, "yymemalign alignment %d, size %d\n", alignment, size);
     if (!inRealMain) return RealX::memalign(alignment, size);

     //thread_local
     inFree = false;
     inAllocation = true;

     PMU_init_check();

     //Data we need for each allocation
     allocation_metadata allocData = init_allocation(size, MALLOC);

     //Do allocation
     void * object = RealX::memalign(alignment, size);
     MemoryWaste::allocUpdate(&allocData, object);
     //Do after
    size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, allocData.size, false);
    incrementMemoryUsage(size, allocData.classSize, new_touched_bytes, object);

    // thread_local
    inAllocation = false;
    return object;
	}


	void * yypvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
	}


	/*
	 * This function returns the total usable size of an object allocated
	 * using glibc malloc (and therefore it does not include the space occupied
	 * by its 8 byte header).
	 */
	size_t getTotalAllocSize(size_t sz) {
		size_t totalSize, usableSize;

		// Smallest possible total object size (including header) is 32 bytes,
		// thus making the total usable object size equal to 32-8=24 bytes.
		if(sz <= 24) {
			return 24;
		}

		// Calculate a total object size that is double-word aligned.
		totalSize = sz + 8;
		if(totalSize % 16 != 0) {
			totalSize = 16 * (((sz + 8) / 16) + 1);
			usableSize = totalSize - 8;
		} else {
			usableSize = sz;
		}

		return usableSize;
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

    if(bibop) {
        if(allocData->size > large_object_threshold) {
            allocData->classSize = allocData->size;
            allocData->classSizeIndex = num_class_sizes -1;

            the_old_classSize = allocData->classSize;
            the_old_classSizeIndex = allocData->classSizeIndex;

            return;
        }
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
        if (allocData->size < large_object_threshold) allocData->classSizeIndex = 0;
        else allocData->classSizeIndex = 1;
        allocData->classSize = malloc_usable_size(uintaddr);
    }
    the_old_classSize = allocData->classSize;
    the_old_classSizeIndex = allocData->classSizeIndex;
}

allocation_metadata init_allocation(size_t sz, enum memAllocType type) {
	PerfReadInfo empty;
	allocation_metadata new_metadata = {
		reused : false,
		//tid : gettid(),
		//tid: thrData.tid,
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

void* myMalloc (size_t size) {
	if (myLocalMemInitialized) {
		return myLocalMalloc(size);
	}

	myMemLock.lock ();
	void* p;
	if((myMemPosition + size + MY_METADATA_SIZE) < TEMP_MEM_SIZE) {
		unsigned * metadata = (unsigned *)(myMem + myMemPosition);
		*metadata = size;
		p = (void *)(myMem + myMemPosition + MY_METADATA_SIZE);
		myMemPosition += size + MY_METADATA_SIZE;
		myMemAllocations++;
	}
	else {
		fprintf(stderr, "Error: myMem out of memory\n");
		fprintf(stderr, "requestedSize= %zu, TEMP_MEM_SIZE= %d, myMemPosition= %lu, myMemAllocations= %lu\n",
				size, TEMP_MEM_SIZE, myMemPosition, myMemAllocations);
		dumpHashmaps();
		abort();
	}
	myMemLock.unlock ();
	return p;
}

void myFree (void* ptr) {
	if (ptr == NULL) return;
	if (ptr >= myLocalMem && ptr <= myLocalMemEnd) {
		myLocalFree(ptr);
		return;
	}
	myMemLock.lock();
	if (ptr >= (void*)myMem && ptr <= myMemEnd) {
		myMemAllocations--;
		if(myMemAllocations == 0) myMemPosition = 0;
	}
	myMemLock.unlock();
}

void* myMalloc_hash (size_t size) {
    if (myLocalMemInitialized) {
        return myLocalMalloc(size);
    }

    myMemLock_hash.lock ();
    void* p;
    if((myMemPosition_hash + size + MY_METADATA_SIZE) < HASH_SIZE) {
        unsigned * metadata = (unsigned *)(myMem_hash + myMemPosition_hash);
        *metadata = size;
        p = (void *)(myMem_hash + myMemPosition_hash + MY_METADATA_SIZE);
        myMemPosition_hash += size + MY_METADATA_SIZE;
        myMemAllocations_hash++;
    }
    else {
        fprintf(stderr, "Error: myMem_hash out of memory\n");
        fprintf(stderr, "requestedSize= %zu, TEMP_MEM_SIZE= %d, myMemPosition_hash= %lu, myMemAllocations_hash= %lu\n",
                size, HASH_SIZE, myMemPosition_hash, myMemAllocations_hash);
        dumpHashmaps();
        abort();
    }
    myMemLock_hash.unlock ();
    return p;
}

void myFree_hash (void* ptr) {
    if (ptr == NULL) return;
    if (ptr >= myLocalMem && ptr <= myLocalMemEnd) {
        myLocalFree(ptr);
        return;
    }
    myMemLock_hash.lock();
    if (ptr >= (void*)myMem_hash && ptr <= myMemEnd_hash) {
        myMemAllocations_hash--;
        if(myMemAllocations_hash == 0) myMemPosition_hash = 0;
    }
    myMemLock_hash.unlock();
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
//	globalTAD.num_mutex_locks += localTAD.num_mutex_locks;
//	globalTAD.num_try_locks += localTAD.num_try_locks;
//	globalTAD.num_spin_locks += localTAD.num_spin_locks;
//	globalTAD.num_spin_trylocks += localTAD.num_spin_trylocks;
	//globalTAD.blowup_bytes += localTAD.blowup_bytes;
	//fprintf(stderr, "%u, %u, %u, %u\n", localTAD.num_mutex_locks, localTAD.num_try_locks, localTAD.num_spin_locks, localTAD.num_spin_trylocks);

//	globalTAD.num_sbrk += localTAD.num_sbrk;
//	globalTAD.num_madvise += localTAD.num_madvise;
//	globalTAD.malloc_mmaps += localTAD.malloc_mmaps;

	//globalTAD.size_sbrk += localTAD.size_sbrk;
	//globalTAD.blowup_allocations += localTAD.blowup_allocations;
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

	globalize_lck.unlock();

    globalized = true;

    MemoryWaste::globalizeMemory();
}

void writeAllocData () {
	fprintf(thrData.output, ">>> large_object_threshold\t%20zu\n", large_object_threshold);
	writeThreadMaps();
	writeThreadContention();
	fflush (thrData.output);
}

void writeThreadMaps () {
///Here

	double numAllocs = safeDivisor(globalTAD.numAllocs);
	double numAllocsFFL = safeDivisor(globalTAD.numAllocsFFL);
	double numFrees = safeDivisor(globalTAD.numFrees);
	double numAllocs_large = safeDivisor(globalTAD.numAllocs_large);
    double numFrees_large = safeDivisor(globalTAD.numFrees_large);

  fprintf (thrData.output, "\n>>>>>>>>>>>>>>>    ALLOCATION NUM    <<<<<<<<<<<<<<<\n");
	fprintf (thrData.output, "small new allocations\t\t\t\t%20lu\n", globalTAD.numAllocs);
	fprintf (thrData.output, "small reused allocations\t\t%20lu\n", globalTAD.numAllocsFFL);
	fprintf (thrData.output, "small deallocations\t\t\t\t\t%20lu\n", globalTAD.numFrees);
    fprintf (thrData.output, "large allocations\t\t\t\t\t\t%20lu\n", globalTAD.numAllocs_large);
    fprintf (thrData.output, "large deallocations\t\t\t\t\t%20lu\n", globalTAD.numFrees_large);
	uint64_t leak;
	if(globalTAD.numAllocs+globalTAD.numAllocsFFL+globalTAD.numAllocs_large > globalTAD.numFrees+globalTAD.numFrees_large) {
	    leak = (globalTAD.numAllocs+globalTAD.numAllocsFFL+globalTAD.numAllocs_large) - (globalTAD.numFrees+globalTAD.numFrees_large);
	} else {
	    leak = 0;
	}
    fprintf (thrData.output, "potential leak num\t\t\t\t\t\t%20lu\n", leak);
	fprintf (thrData.output, "\n");

	MemoryWaste::reportAllocDistribution(thrData.output);

  fprintf (thrData.output, "\n>>>>>>>>>>>>>>>    SMALL NEW ALLOCATIONS    <<<<<<<<<<<<<<<\n");
	fprintf (thrData.output, "cycles\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.cycles_alloc, (globalTAD.cycles_alloc / numAllocs));
	fprintf (thrData.output, "faults\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationFaults, (globalTAD.numAllocationFaults*100 / numAllocs));
	fprintf (thrData.output, "tlb read misses\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbReadMisses, (globalTAD.numAllocationTlbReadMisses*100 / numAllocs));
	fprintf (thrData.output, "tlb write misses\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbWriteMisses, (globalTAD.numAllocationTlbWriteMisses*100 / numAllocs));
	fprintf (thrData.output, "cache misses\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationCacheMisses, (globalTAD.numAllocationCacheMisses / numAllocs));
	fprintf (thrData.output, "instructions\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationInstrs, (globalTAD.numAllocationInstrs / numAllocs));
	fprintf (thrData.output, "\n");

	fprintf (thrData.output, "\n>>>>>>>>>>>>>>>    SMALL FREELIST ALLOCATIONS    <<<<<<<<<<<<<<<\n");
	fprintf (thrData.output, "cycles\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.cycles_allocFFL, (globalTAD.cycles_allocFFL / numAllocsFFL));
	fprintf (thrData.output, "faults\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationFaultsFFL, (globalTAD.numAllocationFaultsFFL*100 / numAllocsFFL));
	fprintf (thrData.output, "tlb read misses\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbReadMissesFFL, (globalTAD.numAllocationTlbReadMissesFFL*100 / numAllocsFFL));
	fprintf (thrData.output, "tlb write misses\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbWriteMissesFFL, (globalTAD.numAllocationTlbWriteMissesFFL*100 / numAllocsFFL));
	fprintf (thrData.output, "cache misses\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationCacheMissesFFL, (globalTAD.numAllocationCacheMissesFFL / numAllocsFFL));
	fprintf (thrData.output, "instructions\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationInstrsFFL, (globalTAD.numAllocationInstrsFFL / numAllocsFFL));
	fprintf (thrData.output, "\n");

	fprintf (thrData.output, "\n>>>>>>>>>>>>>>>    SMALL DEALLOCATIONS    <<<<<<<<<<<<<<<\n");
	fprintf (thrData.output, "cycles\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.cycles_free, (globalTAD.cycles_free / numFrees));
	fprintf (thrData.output, "faults\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationFaults, (globalTAD.numDeallocationFaults*100 / numFrees));
	fprintf (thrData.output, "tlb read misses\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationTlbReadMisses, (globalTAD.numDeallocationTlbReadMisses*100 / numFrees));
	fprintf (thrData.output, "tlb write misses\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationTlbWriteMisses, (globalTAD.numDeallocationTlbWriteMisses*100 / numFrees));
	fprintf (thrData.output, "cache misses\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numDeallocationCacheMisses, (globalTAD.numDeallocationCacheMisses / numFrees));
	fprintf (thrData.output, "instrctions\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numDeallocationInstrs, (globalTAD.numDeallocationInstrs / numFrees));

    fprintf (thrData.output, "\n>>>>>>>>>>>>>>>    LARGE ALLOCATIONS    <<<<<<<<<<<<<<<\n");
    fprintf (thrData.output, "cycles\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.cycles_alloc_large, (globalTAD.cycles_alloc_large / numAllocs_large));
    fprintf (thrData.output, "faults\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationFaults_large, (globalTAD.numAllocationFaults_large*100 / numAllocs_large));
    fprintf (thrData.output, "tlb read misses\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbReadMisses_large, (globalTAD.numAllocationTlbReadMisses_large*100 / numAllocs_large));
    fprintf (thrData.output, "tlb write misses\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numAllocationTlbWriteMisses_large, (globalTAD.numAllocationTlbWriteMisses_large*100 / numAllocs_large));
    fprintf (thrData.output, "cache misses\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationCacheMisses_large, (globalTAD.numAllocationCacheMisses_large / numAllocs_large));
    fprintf (thrData.output, "instructions\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numAllocationInstrs_large, (globalTAD.numAllocationInstrs_large / numAllocs_large));
    fprintf (thrData.output, "\n");

    fprintf (thrData.output, "\n>>>>>>>>>>>>>>>    LARGE DEALLOCATIONS    <<<<<<<<<<<<<<<\n");
    fprintf (thrData.output, "cycles\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.cycles_free_large, (globalTAD.cycles_free_large / numFrees_large));
    fprintf (thrData.output, "faults\t\t\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationFaults_large, (globalTAD.numDeallocationFaults_large*100 / numFrees_large));
    fprintf (thrData.output, "tlb read misses\t\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationTlbReadMisses_large, (globalTAD.numDeallocationTlbReadMisses_large*100 / numFrees_large));
    fprintf (thrData.output, "tlb write misses\t\t\t\t\t\t%20lu\tavg = %0.1lf%%\n", globalTAD.numDeallocationTlbWriteMisses_large, (globalTAD.numDeallocationTlbWriteMisses_large*100 / numFrees_large));
    fprintf (thrData.output, "cache misses\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numDeallocationCacheMisses_large, (globalTAD.numDeallocationCacheMisses_large / numFrees_large));
    fprintf (thrData.output, "instrctions\t\t\t\t\t\t\t\t\t%20lu\tavg = %0.1lf\n", globalTAD.numDeallocationInstrs_large, (globalTAD.numDeallocationInstrs_large / numFrees_large));

    fprintf (thrData.output, "\n>>>>>>>>>>>>>>>     LOCK TOTALS     <<<<<<<<<<<<<<<\n");
    fprintf (thrData.output, "pthread mutex locks\t\t\t\t\t%20u\n", globalTAD.lock_nums[0]);
    fprintf (thrData.output, "pthread spin locks\t\t\t\t\t%20u\n", globalTAD.lock_nums[1]);
    fprintf (thrData.output, "pthread trylocks\t\t\t\t\t\t%20u\n", globalTAD.lock_nums[2]);
    fprintf (thrData.output, "pthread spin trylocks\t\t\t\t%20u\n", globalTAD.lock_nums[3]);

    writeContention();
}

void writeContention () {

		fprintf(thrData.output, "\n>>>>>>>>>>>>>>>>>>>>>>>>> DETAILED LOCK USAGE <<<<<<<<<<<<<<<<<<<<<<<<<\n");
		for(auto lock : lockUsage) {
				PerLockData * data = lock.getValue();
				fprintf(thrData.output, "lockAddr = %#lx\t\ttype = %s\t\tmax contention thread = %10d\t\t",
								lock.getKey(), LockTypeToString(data->type), data->maxContendThreads);
                fprintf(thrData.output, "invocations = %10u\t\tcontention times = %10u\t\tcontention rate = %3u%%\n", data->calls, data->contendCalls, data->contendCalls*100/data->calls);
		}
        fprintf(thrData.output, "\n");
		//fflush(thrData.output);
}

int num_used_pages(uintptr_t vstart, uintptr_t vend) {
	char pagemap_filename[50];
	snprintf (pagemap_filename, 50, "/proc/%d/pagemap", pid);
	int fdmap;
	uint64_t bitmap;
	unsigned long pagenum_start, pagenum_end;
	unsigned num_pages_read = 0;
	unsigned num_pages_to_read = 0;
	unsigned num_used_pages = 0;

	if((fdmap = open(pagemap_filename, O_RDONLY)) == -1) {
		return -1;
	}

	pagenum_start = vstart >> PAGE_BITS;
	pagenum_end = vend >> PAGE_BITS;
	num_pages_to_read = pagenum_end - pagenum_start + 1;
	if(num_pages_to_read == 0) {
		close(fdmap);
		return -1;
	}

	if(lseek(fdmap, (pagenum_start * ENTRY_SIZE), SEEK_SET) == -1) {
		close(fdmap);
		return -1;
	}

	do {
		if(read(fdmap, &bitmap, ENTRY_SIZE) != ENTRY_SIZE) {
			close(fdmap);
			return -1;
		}

		num_pages_read++;
		if((bitmap >> 63) == 1) {
			num_used_pages++;
		}
	} while(num_pages_read < num_pages_to_read);

	close(fdmap);
	return num_used_pages;
}

inline bool isAllocatorInCallStack() {
        // Fetch the frame address of the topmost stack frame
        struct stack_frame * current_frame =
            (struct stack_frame *)(__builtin_frame_address(0));

        // Initialize the prev_frame pointer to equal the current_frame. This
        // simply ensures that the while loop below will be entered and
        // executed and least once
        struct stack_frame * prev_frame = current_frame;

        void * stackEnd = thrData.stackEnd;
		int allocatorLevel = -1;
        int cur_depth = 0;

        while(((void *)prev_frame <= stackEnd) && (prev_frame >= current_frame) &&
				(cur_depth < CALLSITE_MAXIMUM_LENGTH)) {

			void * caller_address = prev_frame->caller_address;

			if(selfmap::getInstance().isAllocator(caller_address))
				allocatorLevel = cur_depth;

            //in some case, "prev" address is the same as current address
            //or there is recursion
            if(prev_frame == prev_frame->prev) {
				cur_depth++;
                break;
            }

            // Walk the prev_frame pointer backward in preparation for the
            // next iteration of the loop
            prev_frame = prev_frame->prev;
            cur_depth++;
		}

	if(allocatorLevel > 0)
		return true;

	return false;
}

void doBefore (allocation_metadata *metadata) {
    //fprintf(stderr, "Dobefore, %d, %d\n", metadata->size, metadata->type);
	getPerfCounts(&(metadata->before));
	metadata->tsc_before = rdtscp();
}

void doAfter (allocation_metadata *metadata) {
    //fprintf(stderr, "Doafter, %d, %d\n", metadata->size, metadata->type);
    metadata->tsc_after = rdtscp();
	getPerfCounts(&(metadata->after));

	metadata->tsc_after -= metadata->tsc_before;
	metadata->after.faults -= metadata->before.faults;
	metadata->after.tlb_read_misses -= metadata->before.tlb_read_misses;
	metadata->after.tlb_write_misses -= metadata->before.tlb_write_misses;
	metadata->after.cache_misses -= metadata->before.cache_misses;
	metadata->after.instructions -= metadata->before.instructions;

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

void incrementGlobalMemoryAllocation(size_t size, size_t classsize) {
  __atomic_add_fetch(&mu.realMemoryUsage, size, __ATOMIC_RELAXED);
  __atomic_add_fetch(&mu.realAllocatedMemoryUsage, classsize, __ATOMIC_RELAXED);
    ///Here
  MemoryWaste::recordMemory((uint64_t)mu.realAllocatedMemoryUsage);
}

void decrementGlobalMemoryAllocation(size_t size, size_t classsize) {
  __atomic_sub_fetch(&mu.realMemoryUsage, size, __ATOMIC_RELAXED);
  __atomic_sub_fetch(&mu.realAllocatedMemoryUsage, classsize, __ATOMIC_RELAXED);
}

void checkGlobalRealMemoryUsage() {
    if(mu.realMemoryUsage > max_mu.realMemoryUsage) {
        max_mu.realMemoryUsage = mu.realMemoryUsage;
    }
}

void checkGlobalAllocatedMemoryUsage() {
    if(mu.realAllocatedMemoryUsage > max_mu.realAllocatedMemoryUsage) {
        max_mu.realAllocatedMemoryUsage = mu.realAllocatedMemoryUsage;
    }
}


void checkGlobalTotalMemoryUsage() {
    if(mu.totalMemoryUsage > max_mu.maxTotalMemoryUsage) {
        max_mu.maxTotalMemoryUsage = mu.totalMemoryUsage;
        max_mu.totalMemoryUsage = mu.totalMemoryUsage;
    }
}

void checkGlobalMemoryUsage() {
  MemoryUsage mu_tmp = mu;
  if(mu_tmp.totalMemoryUsage > max_mu.maxTotalMemoryUsage) {
    mu_tmp.maxTotalMemoryUsage = mu_tmp.totalMemoryUsage;
    max_mu = mu_tmp;
  }
}

void incrementMemoryUsage(size_t size, size_t classSize, size_t new_touched_bytes, void * object) {

		threadContention->realMemoryUsage += size;
		threadContention->realAllocatedMemoryUsage += classSize;
        if(threadContention->realAllocatedMemoryUsage > threadContention->maxRealAllocatedMemoryUsage) {
            threadContention->maxRealAllocatedMemoryUsage = threadContention->realAllocatedMemoryUsage;
        }
        if(threadContention->realMemoryUsage > threadContention->maxRealMemoryUsage) {
            threadContention->maxRealMemoryUsage = threadContention->realMemoryUsage;
        }
		incrementGlobalMemoryAllocation(size, classSize);

    checkGlobalRealMemoryUsage();
    checkGlobalAllocatedMemoryUsage();

		if(new_touched_bytes > 0) {
				threadContention->totalMemoryUsage += new_touched_bytes;
				__atomic_add_fetch(&mu.totalMemoryUsage, new_touched_bytes, __ATOMIC_RELAXED);
				if(threadContention->totalMemoryUsage > threadContention->maxTotalMemoryUsage) {
				    threadContention->maxTotalMemoryUsage = threadContention->totalMemoryUsage;
				}
                checkGlobalTotalMemoryUsage();
		}
}

void decrementMemoryUsage(size_t size, size_t classSize, void * addr) {
  if(addr == NULL) return;

	threadContention->realAllocatedMemoryUsage -= classSize;
	threadContention->realMemoryUsage -= size;

  decrementGlobalMemoryAllocation(size, classSize);

}

void collectAllocMetaData(allocation_metadata *metadata) {
        if (__builtin_expect(!globalized, true)) {
            if (!metadata->reused) {
                if(metadata->size < large_object_threshold) {
                    localTAD.cycles_alloc += metadata->cycles;
                    localTAD.numAllocs++;
                    localTAD.numAllocationFaults += metadata->after.faults;
                    localTAD.numAllocationTlbReadMisses += metadata->after.tlb_read_misses;
                    localTAD.numAllocationTlbWriteMisses += metadata->after.tlb_write_misses;
                    localTAD.numAllocationCacheMisses += metadata->after.cache_misses;
                    localTAD.numAllocationInstrs += metadata->after.instructions;
                } else {
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
                    localTAD.cycles_allocFFL += metadata->cycles;
                    localTAD.numAllocsFFL++;
                    localTAD.numAllocationFaultsFFL += metadata->after.faults;
                    localTAD.numAllocationTlbReadMissesFFL += metadata->after.tlb_read_misses;
                    localTAD.numAllocationTlbWriteMissesFFL += metadata->after.tlb_write_misses;
                    localTAD.numAllocationCacheMissesFFL += metadata->after.cache_misses;
                    localTAD.numAllocationInstrsFFL += metadata->after.instructions;
                } else {
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

void readAllocatorFile() {

	size_t bufferSize = 1024;
	FILE* infile = nullptr;
	char* buffer = (char*) myMalloc(bufferSize);
	char* token;
	fprintf(stderr, "Opening allocator info file %s...\n", allocatorFileName);
	if ((infile = fopen (allocatorFileName, "r")) == NULL) {
		perror("Failed to open allocator info file");
		abort();
	}

	while ((getline(&buffer, &bufferSize, infile)) > 0) {

		token = strtok(buffer, " ");

		if ((strcmp(token, "style")) == 0) {

			token = strtok(NULL, " ");

			if ((strcmp(token, "bibop\n")) == 0) {
				bibop = true;
			} else {
				bumpPointer = true;
			}
			continue;
		} else if ((strcmp(token, "class_sizes")) == 0) {

			token = strtok(NULL, " ");
			num_class_sizes = atoi(token);

			class_sizes = (size_t*) myMalloc (num_class_sizes*sizeof(size_t));
			for (int i = 0; i < num_class_sizes; i++) {
				token = strtok(NULL, " ");
				class_sizes[i] = (size_t) atoi(token);
			}
			continue;
		} else if ((strcmp(token, "large_object_threshold")) == 0) {
			token = strtok(NULL, " ");
			large_object_threshold = (size_t) atoi(token);
			continue;
		}
	}

	myFree(buffer);
}

void* myLocalMalloc(size_t size) {

	void* p;
	if((myLocalPosition + size) < LOCAL_BUF_SIZE) {
		p = (void *)((char*)myLocalMem + myLocalPosition);
		myLocalPosition += size;
		myLocalAllocations++;
	} else {
		fprintf(stderr, "error: myLocalMem out of memory\n");
		fprintf(stderr, "\t requested size = %zu, total size = %d, "
				"total allocs = %lu\n", size, LOCAL_BUF_SIZE, myLocalAllocations);
		dumpHashmaps();
		abort();
	}
	return p;
}

void myLocalFree(void* p) {

	if (p == NULL) return;
	myLocalAllocations--;
	if(myLocalAllocations == 0) myLocalPosition = 0;
}

void initMyLocalMem() {

	myLocalMem = RealX::mmap(NULL, LOCAL_BUF_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (myLocalMem == MAP_FAILED) {
		fprintf (stderr, "Thread %lu failed to mmap myLocalMem\n", myThreadID);
		abort();
	}
	myLocalMemEnd = (void*) ((char*)myLocalMem + LOCAL_BUF_SIZE);
	myLocalPosition = 0;
	myLocalMemInitialized = true;
}

pid_t gettid() {
    return syscall(__NR_gettid);
}

void updateGlobalFriendlinessData() {
		friendly_data * thrFriendlyData = &thrData.friendlyData;

		__atomic_add_fetch(&globalFriendlyData.numAccesses, thrFriendlyData->numAccesses, __ATOMIC_SEQ_CST);
		__atomic_add_fetch(&globalFriendlyData.numCacheWrites, thrFriendlyData->numCacheWrites, __ATOMIC_SEQ_CST);
		//__atomic_add_fetch(&globalFriendlyData.numCacheOwnerConflicts, thrFriendlyData->numCacheOwnerConflicts, __ATOMIC_SEQ_CST);
		__atomic_add_fetch(&globalFriendlyData.numCacheBytes, thrFriendlyData->numCacheBytes, __ATOMIC_SEQ_CST);
		__atomic_add_fetch(&globalFriendlyData.numPageBytes, thrFriendlyData->numPageBytes, __ATOMIC_SEQ_CST);

		#ifdef THREAD_OUTPUT			// DEBUG BLOCK
		if(thrData.output) {
				pid_t tid = thrData.tid;
				double avgCacheUtil = (double) thrFriendlyData->numCacheBytes / (thrFriendlyData->numAccesses * CACHELINE_SIZE);
				double avgPageUtil = (double) thrFriendlyData->numPageBytes / (thrFriendlyData->numAccesses * PAGESIZE);
				FILE * outfd = thrData.output;
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

void calcAppFriendliness() {
		// Final call to update the global data, using this (the main thread's) local data.

    fprintf(thrData.output, "\n>>>>>>>>>>>>>>>>>>>>>>>>> APP FRIENDLINESS <<<<<<<<<<<<<<<<<<<<<<<<<\n");

		updateGlobalFriendlinessData();

		unsigned long totalAccesses = __atomic_load_n(&globalFriendlyData.numAccesses, __ATOMIC_SEQ_CST);
		unsigned long totalCacheWrites = __atomic_load_n(&globalFriendlyData.numCacheWrites, __ATOMIC_SEQ_CST);
		//unsigned long totalCacheOwnerConflicts = __atomic_load_n(&globalFriendlyData.numCacheOwnerConflicts, __ATOMIC_SEQ_CST);
		unsigned long totalCacheBytes = __atomic_load_n(&globalFriendlyData.numCacheBytes, __ATOMIC_SEQ_CST);
		unsigned long totalPageBytes = __atomic_load_n(&globalFriendlyData.numPageBytes, __ATOMIC_SEQ_CST);

		double avgTotalCacheUtil =
		        (double) totalCacheBytes / (totalAccesses * CACHELINE_SIZE);
		double avgTotalPageUtil =
		        (double) totalPageBytes / (totalAccesses * PAGESIZE);
		FILE * outfd = thrData.output;
		//FILE * outfd = stderr;
		fprintf(outfd, "sampled accesses\t\t\t\t\t\t\t\t\t\t\t=%20ld\n", totalAccesses);
		fprintf(outfd, "storing instructions\t\t\t\t\t\t\t\t\t=%20ld\n", totalCacheWrites);
		fprintf(outfd, "cache misses outside allocs and frees\t=%20ld\n", globalTAD.numOutsideCacheMisses);
		fprintf(outfd, "avg. cache utilization\t\t\t\t\t\t\t\t=%19d%%\n", (int)(avgTotalCacheUtil * 100));
		fprintf(outfd, "avg. page utilization\t\t\t\t\t\t\t\t\t=%19d%%\n", (int)(avgTotalPageUtil * 100));
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
