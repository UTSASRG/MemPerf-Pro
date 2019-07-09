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
#include "recordfunctions.hh"

//Globals
bool bibop = false;
bool bumpPointer = false;
bool isLibc = false;
bool inRealMain = false;
bool mapsInitialized = false;
bool opening_maps_file = false;
bool realInitialized = false;
bool selfmapInitialized = false;
std::atomic<bool> creatingThread (false);
bool inConstructor = false;
char* allocator_name;
char* allocatorFileName;
char smaps_fileName[30];
extern char data_start;
extern char _etext;
extern char * program_invocation_name;
extern void * __libc_stack_end;
float memEfficiency = 0;
initStatus profilerInitialized = NOT_INITIALIZED;
pid_t pid;
//size_t alignment = 0;
size_t large_object_threshold = 0;
size_t metadata_object = 0;
size_t metadata_overhead = 0;
size_t total_blowup = 0;
size_t totalSizeAlloc = 0;
size_t totalSizeFree = 0;
size_t totalSizeDiff = 0;
size_t totalMemOverhead = 0;

//Smaps Sampling-------------//
void setupSignalHandler();
void startSignalTimer();
size_t smaps_bufferSize = 1024;
FILE* smaps_infile = nullptr;
char* smaps_buffer;
OverheadSample worstCaseOverhead;
OverheadSample currentCaseOverhead;
SMapEntry* smapsDontCheck[100];
short smapsDontCheckIndex = 0;
short smapsDontCheckNumEntries = 0;
timer_t smap_timer;
uint64_t smap_samples = 0;
uint64_t smap_sample_cycles = 0;
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
std::atomic_bool mmap_active(false);
std::atomic_bool sbrk_active(false);
std::atomic_bool madvise_active(false);
std::atomic<std::size_t> freed_bytes (0);
std::atomic<std::size_t> blowup_bytes (0);
std::atomic<std::size_t> alignment_bytes (0);

thread_local uint64_t total_time_wait = 0;

std::atomic<uint> num_sbrk (0);
std::atomic<uint> num_madvise (0);
std::atomic<uint> malloc_mmaps (0);
std::atomic<std::size_t> size_sbrk (0);

thread_local uint blowup_allocations = 0;
thread_local unsigned long long total_cycles_start = 0;
std::atomic<std::uint64_t> total_global_cycles (0);
std::atomic<std::uint64_t> cycles_alloc (0);
std::atomic<std::uint64_t> cycles_allocFFL (0);
std::atomic<std::uint64_t> cycles_free (0);

MemoryUsage mu;
MemoryUsage max_mu;

/// REQUIRED!
std::atomic<unsigned>* globalFreeArray = nullptr;

//Thread local variables THREAD_LOCAL
thread_local thread_data thrData;
thread_local bool waiting;
thread_local bool inAllocation;
thread_local bool inDeallocation;
thread_local bool inMmap;
thread_local bool PMUinit = false;
thread_local uint64_t timeAttempted;
thread_local uint64_t timeWaiting;
thread_local unsigned* localFreeArray = nullptr;
thread_local bool localFreeArrayInitialized = false;
thread_local uint64_t myThreadID;
thread_local thread_alloc_data localTAD;
thread_alloc_data globalTAD;

thread_local void* myLocalMem;
thread_local void* myLocalMemEnd;
thread_local uint64_t myLocalPosition;
thread_local uint64_t myLocalAllocations;
thread_local bool myLocalMemInitialized = false;
thread_local PerfAppFriendly friendliness;

spinlock globalize_lck;

// Pre-init private allocator memory
char myMem[TEMP_MEM_SIZE];
void* myMemEnd;
uint64_t myMemPosition = 0;
uint64_t myMemAllocations = 0;
spinlock myMemLock;

friendly_data globalFriendlyData;

//Hashmap of class size to TAD*
// typedef HashMap <uint64_t, thread_alloc_data*, spinlock> Class_Size_TAD;

//Hashmap of thread ID to Class_Size_TAD
// HashMap <uint64_t, thread_alloc_data*, spinlock> threadToCSM;

//Hashmap of class size to tad struct, for all thread data summed up
// HashMap<uint64_t, thread_alloc_data*, spinlock> globalCSM;
// HashMap<uint64_t, thread_alloc_data*, spinlock> globalCSM;

//Hashmap of malloc'd addresses to a ObjectTuple
//HashMap <uint64_t, ObjectTuple*, spinlock> addressUsage;

//Hashmap of lock addr to LC
HashMap <uint64_t, LC*, spinlock> lockUsage;

//Hashmap of mmap addrs to tuple:
HashMap <uint64_t, MmapTuple*, spinlock> mappings;

//Hashmap of Overhead objects
HashMap <size_t, Overhead*, spinlock> overhead;

//Hashmap of tid to ThreadContention*
//HashMap <uint64_t, ThreadContention*, spinlock> threadContention;

//Spinlocks
spinlock temp_mem_lock;

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

	lockUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 128);
	overhead.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 256);
	mappings.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	mapsInitialized = true;

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

	readAllocatorFile();

	//Initialize the global and free counter arrays (blowup)
	initGlobalFreeArray();
	initLocalFreeArray();

	// Generate the name of our output file, then open it for writing.
	char outputFile[MAX_FILENAME_LEN];
	snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmallocprof_%d_main_thread.txt",
			program_invocation_name, pid);

	// Will overwrite current file; change the fopen flag to "a" for append.
	thrData.output = fopen(outputFile, "w");
	if(thrData.output == NULL) {
		perror("error: unable to open output file to write");
		return INIT_ERROR;
	}

	fprintf(thrData.output, ">>> stack start @ %p, stack end @ %p\n", thrData.stackStart, thrData.stackEnd);
	fprintf(thrData.output, ">>> program break @ %p\n", program_break);
	fflush(thrData.output);

	if (bibop) {
		//Print class sizes to thrData output
		fprintf (thrData.output, ">>> class_sizes ");

		//Create and init Overhead for each class size
		for (int i = 0; i < num_class_sizes; i++) {

			size_t cs = class_sizes[i];

			fprintf (thrData.output, "%zu ", cs);
			overhead.insert(cs, newOverhead());
		}

		fprintf (thrData.output, "\n");
		fflush(thrData.output);
	}
	else {
		num_class_sizes = 0;
		//Create an entry in the overhead hashmap with key 0
		overhead.insert(BP_OVERHEAD, newOverhead());
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

	worstCaseOverhead.efficiency = 100.00;

	#warning Disabled smaps functionality (timer, signal handler)
	/*
	start_smaps();
	setupSignalHandler();
	startSignalTimer();
	*/

	total_cycles_start = rdtscp();
	inConstructor = false;
	return profilerInitialized;
}

__attribute__((destructor)) void finalizer_mallocprof () {}

void dumpHashmaps() {

	// fprintf(stderr, "addressUsage.printUtilization():\n");
	// addressUsage.printUtilization();

	fprintf(stderr, "overhead.printUtilization():\n");
	overhead.printUtilization();

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

	if(thrData.output) {
			fflush(thrData.output);
	}
	globalizeTAD();
	writeAllocData();
	writeContention();

	// Calculate and print the application friendliness numbers.
	calcAppFriendliness();

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

	#ifndef NO_PMU
	// If the PMU sampler has not yet been set up for this thread, set it up now
	if(!PMUinit){
		initPMU();
		PMUinit = true;
	}
	#endif

	inRealMain = true;
	int result = real_main_mallocprof (argc, argv, envp);
	//    fprintf(stderr, "real main has returned");
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
			return myMalloc (sz);
		}

		//Malloc is being called by a thread that is already in malloc
		if (inAllocation) return RealX::malloc(sz);

		if (!inRealMain) return RealX::malloc(sz);

		//thread_local
		inAllocation = true;

		#ifndef NO_PMU
		// If the PMU sampler has not yet been set up for this thread, set it up now
		if(!PMUinit){
			initPMU();
			PMUinit = true;
		}
		#endif

		if (!localFreeArrayInitialized) initLocalFreeArray();

		//Data we need for each allocation
		allocation_metadata allocData = init_allocation(sz, MALLOC);
		void* object;

		//Do before
		doBefore(&allocData);

		//Do allocation
		object = RealX::malloc(sz);
		allocData.address = (uint64_t) object;

    size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject((void *)allocData.address, allocData.size, false);
		incrementMemoryUsage(sz, new_touched_bytes, object);

		//Do after
		//fprintf(stderr, "*** malloc(%zu) -> %p\n", sz, object);
		doAfter(&allocData);

		allocData.cycles = allocData.tsc_after - allocData.tsc_before;

		// Gets overhead, address usage, mmap usage, memHWM, and prefInfo
		analyzeAllocation(&allocData);

		// thread_local
		inAllocation = false;

		return object;
	}

	void * yycalloc(size_t nelem, size_t elsize) {
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

		// if the PMU sampler has not yet been set up for this thread, set it up now
		#ifndef NO_PMU
		if(!PMUinit){
			initPMU();
			PMUinit = true;
		}
		#endif

		// thread_local
		inAllocation = true;

		if (!localFreeArrayInitialized) {
			initLocalFreeArray();
		}

		// Data we need for each allocation
		allocation_metadata allocData = init_allocation(nelem * elsize, CALLOC);
		void* object;

		// Do before
		doBefore(&allocData);

		// Do allocation
		object = RealX::calloc(nelem, elsize);
		allocData.address = reinterpret_cast <uint64_t> (object);

    size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject((void *)allocData.address, allocData.size, false);
    incrementMemoryUsage(nelem * elsize, new_touched_bytes, object);

		// Do after
		doAfter(&allocData);

		allocData.cycles = allocData.tsc_after - allocData.tsc_before;

		// Gets overhead, address usage, mmap usage, memHWM, and perfInfo
		analyzeAllocation(&allocData);

		//thread_local
		inAllocation = false;

		return object;
	}

	void yyfree(void * ptr) {
		if (!realInitialized) RealX::initializer();
		if(ptr == NULL) return;

		// Determine whether the specified object came from our global memory;
		// only call RealX::free() if the object did not come from here.
		if (ptr >= (void *)myMem && ptr <= myMemEnd) {
			myFree (ptr);
			return;
		}

		if ((profilerInitialized != INITIALIZED) || !inRealMain) {
			myFree(ptr);
			return;
		}

		//if the PMU sampler has not yet been set up for this thread, set it up now
		#ifndef NO_PMU
		if(!PMUinit){
			initPMU();
			PMUinit = true;
		}
		#endif

		//thread_local
		inAllocation = true;

		if (!localFreeArrayInitialized) {
			initLocalFreeArray();
		}

		//Data we need for each free
		allocation_metadata allocData = init_allocation(0, FREE);
		allocData.address = reinterpret_cast <uint64_t> (ptr);

		//fprintf(stderr, "*** free(%p)\n", ptr);
		//Do before free
		doBefore(&allocData);

    decrementMemoryUsage(ptr);
		ShadowMemory::updateObject(ptr, 0, true);

		//Update free counters
		allocData.classSize = updateFreeCounters(ptr);

		//Do free
		RealX::free(ptr);

		//Do after free
		doAfter(&allocData);

    //thread_local
    inAllocation = false;

		cycles_free += (allocData.tsc_after - allocData.tsc_before);

		localTAD.numFrees++;
		localTAD.cycles_free += (allocData.tsc_after - allocData.tsc_before);
		localTAD.numDeallocationFaults += allocData.after.faults - allocData.before.faults;
		localTAD.numDeallocationTlbReadMisses += allocData.after.tlb_read_misses - allocData.before.tlb_read_misses;
		localTAD.numDeallocationTlbWriteMisses += allocData.after.tlb_write_misses - allocData.before.tlb_write_misses;
		localTAD.numDeallocationCacheMisses += allocData.after.cache_misses - allocData.before.cache_misses;
		localTAD.numDeallocationInstrs += allocData.after.instructions - allocData.before.instructions;

		/*
		// DEBUG BLOCK
		if((allocData.after.instructions - allocData.before.instructions) != 0) {
			fprintf(stderr, "Free from thread %d\n"
					"Num faults:              %ld\n"
					"Num TLB read misses:     %ld\n"
					"Num TLB write misses:    %ld\n"
					"Num cache misses:        %ld\n"
					"Num instructions:        %ld\n\n",
					allocData.tid, allocData.after.faults - allocData.before.faults,
					allocData.after.tlb_read_misses - allocData.before.tlb_read_misses,
					allocData.after.tlb_write_misses - allocData.before.tlb_write_misses,
					allocData.after.cache_misses - allocData.before.cache_misses,
					allocData.after.instructions - allocData.before.instructions);
		}
		*/
	}

	void * yyrealloc(void * ptr, size_t sz) {

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

		//if the PMU sampler has not yet been set up for this thread, set it up now
		#ifndef NO_PMU
		if(!PMUinit){
			initPMU();
			PMUinit = true;
		}
		#endif

		//Data we need for each allocation
		allocation_metadata allocData = init_allocation(sz, REALLOC);

		// allocated object
		void * object;

		//thread_local
		inAllocation = true;

		if(!localFreeArrayInitialized) {
			initLocalFreeArray();
		}

		//Do before
		doBefore(&allocData);

    if(ptr) {
        ShadowMemory::updateObject(ptr, 0, true);
    }

		//Do allocation
		object = RealX::realloc(ptr, sz);
		allocData.address = reinterpret_cast <uint64_t> (object);

		//Do after
    size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject(object, sz, false);
		if(object != ptr) {
      incrementMemoryUsage(sz, new_touched_bytes, object);
    }
		doAfter(&allocData);

		// cyclesForRealloc = tsc_after - tsc_before;
		allocData.cycles = allocData.tsc_after - allocData.tsc_before;

		//Gets overhead, address usage, mmap usage
		// analyzeAllocation(sz, address, cyclesForRealloc, classSize, &reused);
		analyzeAllocation(&allocData);

		//Get perf info
		//analyzePerfInfo(&before, &after, classSize, &reused, tid);

		//thread_local
		inAllocation = false;

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
    inAllocation = true;

    #ifndef NO_PMU
    // If the PMU sampler has not yet been set up for this thread, set it up now
    if(!PMUinit){
      initPMU();
      PMUinit = true;
    }
    #endif

    if (!localFreeArrayInitialized) initLocalFreeArray();

    //Data we need for each allocation
    allocation_metadata allocData = init_allocation(size, MALLOC);

    //Do allocation
    int retval = RealX::posix_memalign(memptr, alignment, size);
    void * object = *memptr;
    allocData.address = (uint64_t) object;

    //Do after
    size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject((void *)allocData.address, allocData.size, false);
    incrementMemoryUsage(size, new_touched_bytes, object);

    // thread_local
    inAllocation = false;

    return retval;
	}
	void * yyaligned_alloc(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}
 void * yymemalign(size_t alignment, size_t size) {
    if (!inRealMain) return RealX::memalign(alignment, size);

    //thread_local
    inAllocation = true;

    #ifndef NO_PMU
    // If the PMU sampler has not yet been set up for this thread, set it up now
    if(!PMUinit){
      initPMU();
      PMUinit = true;
    }
    #endif

    if (!localFreeArrayInitialized) initLocalFreeArray();

    //Data we need for each allocation
    allocation_metadata allocData = init_allocation(size, MALLOC);

    //Do allocation
    void * object = RealX::memalign(alignment, size);
    allocData.address = (uint64_t) object;

    //Do after
    size_t new_touched_bytes = PAGESIZE * ShadowMemory::updateObject((void *)allocData.address, allocData.size, false);
    incrementMemoryUsage(size, new_touched_bytes, object);

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
        //fprintf(stderr, "Thread: %lX\n", thread);
		return result;
	}
}//End of extern "C"

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

void analyzeAllocation(allocation_metadata *metadata) {
	//Analyzes for alignment and blowup
	getOverhead(metadata->size, metadata->address, metadata->classSize, &(metadata->reused));

	// Analyze perfinfo
	analyzePerfInfo(metadata);
}

/*
	updateFreeCounters

	This function updates the local and global counters of free objects
	needed for determing blowup. If libc, increment a global "freed_bytes"
	variable
*/
size_t updateFreeCounters(void * address) {

	size_t current_class_size = 0;

	if (bibop){
			size_t size = ShadowMemory::getObjectSize(address);
			current_class_size = getClassSizeFor(size);
			short class_size_index = getClassSizeIndex(size);
			localFreeArray[class_size_index]++;
			globalFreeArray[class_size_index]++;
	} else {
		size_t size = malloc_usable_size(address);
		current_class_size = size;
		freed_bytes += size;
	}

	return current_class_size;
}

/*
	getClassSizeIndex

	This function finds the class size for the given size
	and returns the index into the class_sizes array for
	this class size. 

	This is used in updateFreeCounters. The local and global free
	counter arrays are "identical" to the class_sizes array.
	So we can call localFreeArray[index] and globalFreeArray[index]
*/
short getClassSizeIndex(size_t size) {
	if (bumpPointer) {
		if (size < LARGE_OBJECT) return 0;
		else return 1;
	}

	short class_size_index = 0;
	for (int i = 0; i < num_class_sizes; i++) {
		if (size > class_sizes[i]) continue;
		class_size_index = i;
		break;
	}
	return class_size_index;
}

/*
	getOverhead

	Calculates alignment due to class size
*/
void getOverhead (size_t size, uint64_t address, size_t classSize, bool* reused) {

	//Check for classSize alignment bytes
	// if (bibop) getAlignment(size, classSize);
	getAlignment(size, classSize);

	//Check for memory blowup
	getBlowup(size, classSize, reused);
}

void getMetadata (size_t classSize) {

	Overhead* o;
	if (overhead.find(classSize, &o)) {
		o->addMetadata(metadata_object);
	}
}

void getAlignment (size_t size, size_t classSize) {

		Overhead* o;
		short alignmentBytes = classSize - size;
		if (overhead.find(classSize, &o)) {
				if (bibop) {
						o->addAlignment(alignmentBytes);
						alignment_bytes += alignmentBytes;
				}
				else {
						unsigned y = size + (~(size & 0xf) & 0xf) + 1;
						unsigned alignment;
						if ((y - size) >= 8) alignment = (y - size - 8);
						else alignment = (y - size + 8);

						o->addAlignment(alignment);
						alignment_bytes += alignment;
				}

		}
}

void getBlowup (size_t size, size_t classSize, bool* reused) {

	short class_size_index = getClassSizeIndex(size);
	size_t blowup = 0;

	if (bibop) {
		//If I have free objects on my local list
		if (localFreeArray[class_size_index] > 0) {
			*reused = true;
			localFreeArray[class_size_index]--;
			if (globalFreeArray[class_size_index] > 0) globalFreeArray[class_size_index]--;
			return;
		}

		//I don't have free objects on my local list, check global
		else if (globalFreeArray[class_size_index] > 0) {

			//I didn't have free, but someone else did
			//Log this as blowup. Don't return

			globalFreeArray[class_size_index]--;
		}
		//I don't have free objects, and no one else does either
		else return;
	} //End if (bibop)

	else { //This is bumpPointer

		//If I don't have enough freed bytes to satisfy this size
		//set blowup
		if (size > freed_bytes) {
				blowup = size - freed_bytes;
				freed_bytes = 0;
		}

		//I do have enough bytes
		//Decrement my free bytes, and return
		//Don't "log" a blowup allocation
		else {
			*reused = true;
			freed_bytes -= size;
			return;
		}
	}

	//If we've made it here, this is a blowup allocation
	//Increment the blowup for this class size
	Overhead* o;
	if (overhead.find(classSize, &o)) {
			if (bibop) {
					o->addBlowup(classSize);
					blowup_bytes += classSize;
			}
			else {
					o->addBlowup(blowup);
					blowup_bytes += blowup;
			}
	} else {
			blowup_bytes += classSize;
			if(classSize < large_object_threshold) {
					fprintf(stderr, "ERROR: could not find size class %zu in overhead hashmap\n", classSize);
					abort();
			}
	}
}

//Return the appropriate class size that this object should be in
size_t getClassSizeFor (size_t size) {

	if(size > large_object_threshold) {
			return size;
	}

	size_t sizeToReturn = 0;
	if (bibop) {
		if (num_class_sizes == 0) {
			fprintf (stderr, "num_class_sizes == 0. Figure out why. Abort()\n");
			abort();
		}

		for (int i = 0; i < num_class_sizes; i++) {

			size_t tempSize = class_sizes[i];;
			if (size > tempSize) continue;
			else if (size <= tempSize) {
				sizeToReturn = tempSize;
				break;
			}
		}
	}
	return sizeToReturn;
}

// Create tuple for hashmap
ObjectTuple* newObjectTuple (uint64_t address, size_t size) {

	ObjectTuple* t = (ObjectTuple*) myMalloc (sizeof (ObjectTuple));
	t->szUsed = size;

	return t;
}

MmapTuple* newMmapTuple (uint64_t address, size_t length, int prot, char origin) {

	MmapTuple* t = (MmapTuple*) myMalloc (sizeof (MmapTuple));

	uint64_t end = (address + length) - 1;
	t->start = address;
	t->end = end;
	t->length = length;
	t->rw = 0;
	t->origin = origin;
	if (prot == (PROT_READ | PROT_WRITE)) t->rw += length;
	else if (prot == (PROT_READ | PROT_WRITE | PROT_EXEC)) t->rw += length;
	t->tid = gettid();
	t->allocations = 0;
	return t;
}

LC* newLC (LockType lockType, int contention) {
	LC* lc = (LC*) myMalloc(sizeof(LC));
	lc->contention = contention;
	lc->maxContention = contention;
	lc->lockType = lockType;
	return lc;
}

Overhead* newOverhead () {

	Overhead* o = (Overhead*) myMalloc(sizeof(Overhead));
	o->init();
	return o;
}

allocation_metadata init_allocation(size_t sz, enum memAllocType type) {
	PerfReadInfo empty;
	allocation_metadata new_metadata = {
		reused : false,
		tid : gettid(),
		before : empty,
		after : empty,
		size : sz,
		classSize : getClassSizeFor(sz),
		cycles : 0,
		address : 0,
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

void globalizeTAD() {
	globalize_lck.lock();
	globalTAD.numAllocationFaults += localTAD.numAllocationFaults;
	globalTAD.numAllocationTlbReadMisses += localTAD.numAllocationTlbReadMisses;
	globalTAD.numAllocationTlbWriteMisses += localTAD.numAllocationTlbWriteMisses;
	globalTAD.numAllocationCacheMisses += localTAD.numAllocationCacheMisses;
	globalTAD.numAllocationInstrs += localTAD.numAllocationInstrs;

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

	globalTAD.num_mutex_locks += localTAD.num_mutex_locks;
	globalTAD.num_try_locks += localTAD.num_try_locks;
	globalTAD.num_spin_locks += localTAD.num_spin_locks;
	globalTAD.num_spin_trylocks += localTAD.num_spin_trylocks;
	globalTAD.blowup_bytes += localTAD.blowup_bytes;

	globalTAD.num_sbrk += localTAD.num_sbrk;
	globalTAD.num_madvise += localTAD.num_madvise;
	globalTAD.malloc_mmaps += localTAD.malloc_mmaps;

	globalTAD.size_sbrk += localTAD.size_sbrk;
	globalTAD.blowup_allocations += localTAD.blowup_allocations;
	globalTAD.cycles_alloc += localTAD.cycles_alloc;
	globalTAD.cycles_allocFFL += localTAD.cycles_allocFFL;
	globalTAD.cycles_free += localTAD.cycles_free;

	globalTAD.numAllocs += localTAD.numAllocs;
	globalTAD.numAllocsFFL += localTAD.numAllocsFFL;
	globalTAD.numFrees += localTAD.numFrees;

	globalize_lck.unlock();
}

void writeAllocData () {

	fprintf(thrData.output, ">>> malloc_mmaps               %20u\n", malloc_mmaps.load());
	fprintf(thrData.output, ">>> large_object_threshold     %20zu\n", large_object_threshold);
	fprintf(thrData.output, ">>> num_madvise                %20u\n", num_madvise.load());
	fprintf(thrData.output, ">>> num_sbrk                   %20u\n", num_sbrk.load());
	fprintf(thrData.output, ">>> size_sbrk                  %20zu\n", size_sbrk.load());
	fprintf(thrData.output, ">>> cycles_alloc               %20lu\n", cycles_alloc.load());
	fprintf(thrData.output, ">>> cycles_allocFFL            %20lu\n", cycles_allocFFL.load());
	fprintf(thrData.output, ">>> cycles_free                %20lu\n", cycles_free.load());
	fprintf(thrData.output, ">>> total_global_cycles        %20lu\n", total_global_cycles.load());
	//fprintf(thrData.output, ">>> alignment                %20zu\n", alignment);

	// writeOverhead();

	// writeMappings();
	writeThreadMaps();

	writeThreadContention();

	fflush (thrData.output);
}

void writeThreadMaps () {
	double numAllocs = safeDivisor(globalTAD.numAllocs);
	double numAllocsFFL = safeDivisor(globalTAD.numAllocsFFL);
	double numFrees = safeDivisor(globalTAD.numFrees);

  fprintf (thrData.output, "\n>>>>>>>>>>>>>>>    NEW ALLOCATIONS : total (average)    <<<<<<<<<<<<<<<\n");
	fprintf (thrData.output, "allocation cycles              %20lu    avg = %0.1lf\n", globalTAD.cycles_alloc, (globalTAD.cycles_alloc / numAllocs));
	fprintf (thrData.output, "allocation faults              %20lu    avg = %0.1lf\n", globalTAD.numAllocationFaults, (globalTAD.numAllocationFaults / numAllocs));
	fprintf (thrData.output, "allocation tlb read misses     %20lu    avg = %0.1lf\n", globalTAD.numAllocationTlbReadMisses, (globalTAD.numAllocationTlbReadMisses / numAllocs));
	fprintf (thrData.output, "allocation tlb write misses    %20lu    avg = %0.1lf\n", globalTAD.numAllocationTlbWriteMisses, (globalTAD.numAllocationTlbWriteMisses / numAllocs));
	fprintf (thrData.output, "allocation cache misses        %20lu    avg = %0.1lf\n", globalTAD.numAllocationCacheMisses, (globalTAD.numAllocationCacheMisses / numAllocs));
	fprintf (thrData.output, "num allocation instr           %20lu    avg = %0.1lf\n", globalTAD.numAllocationInstrs, (globalTAD.numAllocationInstrs / numAllocs));
	fprintf (thrData.output, "\n");

	fprintf (thrData.output, "\n>>>>>>>>>>>>>>>  FREELIST ALLOCATIONS : total (average) <<<<<<<<<<<<<<<\n");
	fprintf (thrData.output, "allocation cycles              %20lu    avg = %0.1lf\n", globalTAD.cycles_allocFFL, (globalTAD.cycles_allocFFL / numAllocsFFL));
	fprintf (thrData.output, "allocation faults              %20lu    avg = %0.1lf\n", globalTAD.numAllocationFaultsFFL, (globalTAD.numAllocationFaultsFFL / numAllocsFFL));
	fprintf (thrData.output, "allocation tlb read misses     %20lu    avg = %0.1lf\n", globalTAD.numAllocationTlbReadMissesFFL, (globalTAD.numAllocationTlbReadMissesFFL / numAllocsFFL));
	fprintf (thrData.output, "allocation tlb write misses    %20lu    avg = %0.1lf\n", globalTAD.numAllocationTlbWriteMissesFFL, (globalTAD.numAllocationTlbWriteMissesFFL / numAllocsFFL));
	fprintf (thrData.output, "allocation cache misses        %20lu    avg = %0.1lf\n", globalTAD.numAllocationCacheMissesFFL, (globalTAD.numAllocationCacheMissesFFL / numAllocsFFL));
	fprintf (thrData.output, "num allocation instr           %20lu    avg = %0.1lf\n", globalTAD.numAllocationInstrsFFL, (globalTAD.numAllocationInstrsFFL / numAllocsFFL));
	fprintf (thrData.output, "\n");

	fprintf (thrData.output, "\n>>>>>>>>>>>>>>>     DEALLOCATIONS : total (average)     <<<<<<<<<<<<<<<\n");
	fprintf (thrData.output, "deallocation cycles            %20lu    avg = %0.1lf\n", globalTAD.cycles_free, (globalTAD.cycles_free / numFrees));
	fprintf (thrData.output, "deallocation faults            %20lu    avg = %0.1lf\n", globalTAD.numDeallocationFaults, (globalTAD.numDeallocationFaults / numFrees));
	fprintf (thrData.output, "deallocation tlb read misses   %20lu    avg = %0.1lf\n", globalTAD.numDeallocationTlbReadMisses, (globalTAD.numDeallocationTlbReadMisses / numFrees));
	fprintf (thrData.output, "deallocation tlb write misses  %20lu    avg = %0.1lf\n", globalTAD.numDeallocationTlbWriteMisses, (globalTAD.numDeallocationTlbWriteMisses / numFrees));
	fprintf (thrData.output, "deallocation cache misses      %20lu    avg = %0.1lf\n", globalTAD.numDeallocationCacheMisses, (globalTAD.numDeallocationCacheMisses / numFrees));
	fprintf (thrData.output, "num deallocation instr         %20lu    avg = %0.1lf\n", globalTAD.numDeallocationInstrs, (globalTAD.numDeallocationInstrs / numFrees));

	fprintf (thrData.output, "\n>>>>>>>>>>>>>>>               LOCK TOTALS               <<<<<<<<<<<<<<<\n");
	fprintf (thrData.output, "num pthread mutex locks        %20u\n", globalTAD.num_mutex_locks);
	fprintf (thrData.output, "num pthread trylocks           %20u\n", globalTAD.num_try_locks);
	fprintf (thrData.output, "num pthread spin locks         %20u\n", globalTAD.num_spin_locks);
	fprintf (thrData.output, "num pthread spin trylocks      %20u\n", globalTAD.num_spin_trylocks);
}

void writeOverhead () {

	fprintf (thrData.output, "\n-------------Overhead-------------\n");
	for (auto o : overhead) {

		auto key = o.getKey();
		auto data = o.getData();
		char size[10];
		sprintf (size, "%zu", key);
		fprintf (thrData.output, "classSize %s:\nmetadata %zu\nblowup %zu\nalignment %zu\n\n",
				key ? size : "BumpPointer", data->getMetadata(), data->getBlowup(), data->getAlignment());
	}
}

void writeContention () {

		fprintf(thrData.output, "\n>>>>>>>>>>>>>>>>>>>>>>>>> Detailed Lock Usage <<<<<<<<<<<<<<<<<<<<<<<<<\n");
		for(auto lock : lockUsage) {
				LC * data = lock.getData();
				fprintf(thrData.output, "lockAddr = %#lx, type = %s, maxContention = %d\n",
								lock.getKey(), LockTypeToString(data->lockType), data->maxContention.load(relaxed));
		}

		fflush(thrData.output);
}

void writeMappings () {

	int i = 0;
	fprintf (thrData.output, "\n------------mappings------------\n");
	for (auto r : mappings) {
		auto data = r.getData();
		fprintf (thrData.output,
				"Region[%d]: origin= %c, start= %#lx, end= %#lx, length= %zu, "
				"tid= %d, rw= %lu, allocations= %u\n",
				i, data->origin, data->start, data->end, data->length,
				data->tid, data->rw, data->allocations.load(relaxed));
		i++;
	}
}

void calculateMemOverhead () {

	for (auto o : overhead) {

		auto data = o.getData();
		//alignment += data->getAlignment();
		total_blowup += data->getBlowup();
	}

	// totalMemOverhead += alignment;
	// totalMemOverhead += total_blowup;

	// totalSizeAlloc = totalMemAllocated.load();
	//Calculate metadata_overhead by using the per-object value
	// uint64_t allocations = (num_malloc.load() + num_calloc.load() + num_realloc.load());
	// metadata_overhead = (allocations * metadata_object);

	// totalMemOverhead += metadata_overhead;
	// memEfficiency = ((float) totalSizeAlloc / (totalSizeAlloc + totalMemOverhead)) * 100;
}

/*
 * Returns the number of virtual pages that are backed by physical frames
 * in the given mapping (note that this is not the same thing as the number
 * of distinct physical pages used within the mapping, as we do not account
 * for the possibility of multiply-used frames).
 *
 * Returns -1 on error, or the number of pages on success.
 */
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

bool mappingEditor (void* addr, size_t len, int prot) {

	bool found = false;
	for (auto mapping : mappings) {
		auto tuple = mapping.getData();
		uint64_t address = (uint64_t) addr;
		if ((tuple->start <= address) && (address <= tuple->end)) {
			found = true;
			if (prot == (PROT_READ | PROT_WRITE)) {
				tuple->rw += len;
			}
			else if (prot == PROT_NONE) {
				tuple->rw -= len;
			}
		}
	}
	return found;
}

void doBefore (allocation_metadata *metadata) {
	getPerfCounts(&(metadata->before), true);
	metadata->tsc_before = rdtscp();
}

void doAfter (allocation_metadata *metadata) {
	getPerfCounts(&(metadata->after), false);
	metadata->tsc_after = rdtscp();
}

void incrementGlobalMemoryAllocation(size_t size, size_t classsize) {
  __atomic_add_fetch(&mu.realMemoryUsage, size, __ATOMIC_RELAXED);
  __atomic_add_fetch(&mu.realAllocatedMemoryUsage, classsize, __ATOMIC_RELAXED);
}

void decrementGlobalMemoryAllocation(size_t size, size_t classsize) {
  __atomic_sub_fetch(&mu.realMemoryUsage, size, __ATOMIC_RELAXED);
  __atomic_sub_fetch(&mu.realAllocatedMemoryUsage, classsize, __ATOMIC_RELAXED);
}

void checkGlobalMemoryUsage() {
  MemoryUsage mu_tmp = mu;
  if(mu_tmp.totalMemoryUsage > max_mu.maxTotalMemoryUsage) {
    mu_tmp.maxTotalMemoryUsage = mu_tmp.totalMemoryUsage;
    max_mu = mu_tmp;
  }
}

void incrementMemoryUsage(size_t size, size_t new_touched_bytes, void * object) {
		size_t classSize = 0;
		if(bibop) {
				classSize = getClassSizeFor(size);
		} else {
				classSize = malloc_usable_size(object);
		}

		current_tc->realMemoryUsage += size;
		current_tc->realAllocatedMemoryUsage += classSize;

		incrementGlobalMemoryAllocation(size, classSize);

		if(new_touched_bytes > 0) {
				current_tc->totalMemoryUsage += new_touched_bytes;
				__atomic_add_fetch(&mu.totalMemoryUsage, new_touched_bytes, __ATOMIC_RELAXED);
				if(current_tc->totalMemoryUsage > current_tc->maxTotalMemoryUsage) {
						current_tc->maxRealAllocatedMemoryUsage = current_tc->realAllocatedMemoryUsage;
						current_tc->maxRealMemoryUsage = current_tc->realMemoryUsage;
						current_tc->maxTotalMemoryUsage = current_tc->totalMemoryUsage;
				}

				checkGlobalMemoryUsage();
		}
}

void decrementMemoryUsage(void* addr) {
  if(addr == NULL) return;
  
  unsigned classSize;
  if(isLibc) {
			classSize = malloc_usable_size(addr);
  } else {
			classSize = ShadowMemory::getPageClassSize(addr);
	}

  size_t size = ShadowMemory::getObjectSize(addr);
	current_tc->realAllocatedMemoryUsage -= classSize;
	current_tc->realMemoryUsage -= size;

  decrementGlobalMemoryAllocation(size, classSize);

	#warning is this call really necessary?
  checkGlobalMemoryUsage();
}

void collectAllocMetaData(allocation_metadata *metadata) {
	if (!metadata->reused) {
		cycles_alloc += metadata->cycles;
		localTAD.cycles_alloc += metadata->cycles;
		localTAD.numAllocs++;
		localTAD.numAllocationFaults += metadata->after.faults - metadata->before.faults;
		localTAD.numAllocationTlbReadMisses += metadata->after.tlb_read_misses - metadata->before.tlb_read_misses;
		localTAD.numAllocationTlbWriteMisses += metadata->after.tlb_write_misses - metadata->before.tlb_write_misses;
		localTAD.numAllocationCacheMisses += metadata->after.cache_misses - metadata->before.cache_misses;
		localTAD.numAllocationInstrs += metadata->after.instructions - metadata->before.instructions;
	} else {
		cycles_allocFFL += metadata->cycles;
		localTAD.cycles_allocFFL += metadata->cycles;
		localTAD.numAllocsFFL++;
		localTAD.numAllocationFaultsFFL += metadata->after.faults - metadata->before.faults;
		localTAD.numAllocationTlbReadMissesFFL += metadata->after.tlb_read_misses - metadata->before.tlb_read_misses;
		localTAD.numAllocationTlbWriteMissesFFL += metadata->after.tlb_write_misses - metadata->before.tlb_write_misses;
		localTAD.numAllocationCacheMissesFFL += metadata->after.cache_misses - metadata->before.cache_misses;
		localTAD.numAllocationInstrsFFL += metadata->after.instructions - metadata->before.instructions;
	}
}

void analyzePerfInfo(allocation_metadata *metadata) {
	collectAllocMetaData(metadata);

	/*
	// DEBUG BLOCK
	if((metadata->after.instructions - metadata->before.instructions) != 0) {
		fprintf(stderr, "Malloc from thread       %d\n"
				"From free list:          %s\n"
				"Num faults:              %ld\n"
				"Num TLB read misses:     %ld\n"
				"Num TLB write misses:    %ld\n"
				"Num cache misses:        %ld\n"
				"Num instructions:        %ld\n\n",
				metadata->tid, metadata->reused ? "true" : "false",
				metadata->after.faults - metadata->before.faults,
				metadata->after.tlb_read_misses - metadata->before.tlb_read_misses,
				metadata->after.tlb_write_misses - metadata->before.tlb_write_misses,
				metadata->after.cache_misses - metadata->before.cache_misses,
				metadata->after.instructions - metadata->before.instructions);
	}
	*/
}

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

void initLocalFreeArray () {
	if (bibop) {
		localFreeArray = (unsigned*) myMalloc(num_class_sizes*sizeof(unsigned));
	} else {
		localFreeArray = (unsigned*) myMalloc(2*sizeof(unsigned));
	}
	localFreeArrayInitialized = true;
}

void initGlobalFreeArray () {
	if (bibop) {
		globalFreeArray = (std::atomic<unsigned>*) myMalloc(num_class_sizes*sizeof(unsigned));
	} else {
		globalFreeArray = (std::atomic<unsigned>*) myMalloc(2*sizeof(unsigned));
	}
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

void sampleMemoryOverhead(int i, siginfo_t* s, void* p) {

	size_t alignBytes = alignment_bytes.load();
	size_t blowupBytes = blowup_bytes.load();

	/*
	if (timer_settime(smap_timer, 0, &stopTimer, NULL) == -1) {
		perror("timer_settime failed");
		abort();
	}
	*/

	uint64_t startTime = rdtscp();
	smap_samples++;
	int numSmapEntries = 0;
	int numSkips = 0;
	int numHeapMatches = 0;
	bool dontCheck = false;
	bool isStack = false;
	void* start;
	void* end;
	unsigned kb = 0;
	smapsDontCheckIndex = 0;
	char stack[] = "stack";

	currentCaseOverhead.kb = 0;
	currentCaseOverhead.efficiency = 0;

	fseek(smaps_infile, 0, SEEK_SET);

	while ((getline(&smaps_buffer, &smaps_bufferSize, smaps_infile)) > 0) {

		dontCheck = false;
		numSmapEntries++;
		if (strstr(smaps_buffer, (const char*)&stack) != NULL) isStack = true;

		//Get addresses
		sscanf(smaps_buffer, "%p-%p", &start, &end);

		SMapEntry* entry;
		short index = smapsDontCheckIndex;

		//Compare against smapsDontCheck
		while (index < smapsDontCheckNumEntries) {

			entry = smapsDontCheck[index];

			//If it's in smapsDontCheck, skip to next entry in smaps
			if (start == entry->start) {
				dontCheck = true;
				numSkips++;
				smapsDontCheckIndex++;
				for (int j = 0; j < 15; j++)
					getline(&smaps_buffer, &smaps_bufferSize, smaps_infile);
				break;
			}
			index++;
		}

		if (dontCheck) continue;

		bool match = false;

		if (isStack) {
			for (int j = 0; j < 2; j++)
				getline(&smaps_buffer, &smaps_bufferSize, smaps_infile);

			//Grab RSS
			sscanf(smaps_buffer, "Rss:%u", &kb);

			//This is a heap segment
			currentCaseOverhead.kb += kb;
			numHeapMatches++;
			match = true;
			goto skipSearchMappings;
		}

		//Compare against known heaps
		for (auto entry : mappings) {
			auto data = entry.getData();
			if ((data->start >= (uint64_t)start) && (data->start <= (uint64_t)end)) {
				//Skip two lines
				for (int j = 0; j < 2; j++)
					getline(&smaps_buffer, &smaps_bufferSize, smaps_infile);

				//Grab RSS
				sscanf(smaps_buffer, "Rss:%u", &kb);

				//This is a heap segment
				currentCaseOverhead.kb += kb;
				numHeapMatches++;
				match = true;
				break;
			}
			else continue;
		}

		skipSearchMappings:
		if (match) {
			for (int j = 0; j < 13; j++)
				getline(&smaps_buffer, &smaps_bufferSize, smaps_infile);
			continue;
		}
		else {
			//This is an unknown entry, skip it
			for (int j = 0; j < 15; j++)
				getline(&smaps_buffer, &smaps_bufferSize, smaps_infile);
		}
	}

	//Get current efficiency
	uint64_t bytes = currentCaseOverhead.kb*1024;
	double e = ((bytes - blowupBytes - alignBytes) / (float)bytes);

	//Compare to current worst case
	if (!(e >= 1) && e < worstCaseOverhead.efficiency) {
		worstCaseOverhead.kb = bytes;
		worstCaseOverhead.alignment = alignBytes;
		worstCaseOverhead.blowup = blowupBytes;
		worstCaseOverhead.efficiency = e;
	}
	uint64_t endTime = rdtscp();

	smap_sample_cycles += (endTime - startTime);

	/*
	fprintf(stderr, "leaving sample, %d smap entries\n"
	                "bytes= %lu, blowup= %zu, alignment= %zu\n"
	                "skips= %d, hits= %d, cycles: %lu\nefficiency= %.5f\n",
	                numSmapEntries, bytes, blowupBytes, alignBytes,
	                numSkips, numHeapMatches, (endTime-startTime), e);
	*/

	/*
	if (timer_settime(smap_timer, 0, &resumeTimer, NULL) == -1) {
		perror("timer_settime failed");
		abort();
	}
	*/
}

void start_smaps() {

	bool debug = false;
	if (debug) fprintf(stderr, "Reading smaps on startup, pid %d\n", pid);
	void* start;
	void* end;
	unsigned kb;
	char heap[] = "heap";
	bool keep = false;

	if ((smaps_infile = fopen (smaps_fileName, "r")) == NULL) {
		fprintf(stderr, "failed to open smaps\n");
		return;
	}

	while ((getline(&smaps_buffer, &smaps_bufferSize, smaps_infile)) > 0) {
		keep = false;
		if (strstr(smaps_buffer, (const char*)&heap) != NULL) {
			keep = true;
		}
		sscanf(smaps_buffer, "%p-%p", &start, &end);
		if (keep) mappings.insert((uint64_t)start, (newMmapTuple((uint64_t)start, 0, PROT_READ | PROT_WRITE, 'a')));

		//Skip next line
		getline(&smaps_buffer, &smaps_bufferSize, smaps_infile);

		getline(&smaps_buffer, &smaps_bufferSize, smaps_infile);
		sscanf(smaps_buffer, "%*s%u", &kb);

		//Skip next 13 Lines
		for (int i = 0; i < 13; i++)
			getline(&smaps_buffer, &smaps_bufferSize, smaps_infile);

		SMapEntry* entry = newSMapEntry();
		entry->start = start;
		entry->end = end;
		entry->kb = kb;
		if (!keep) {
			smapsDontCheck[smapsDontCheckIndex] = entry;
			smapsDontCheckIndex++;
		}
	}

	smapsDontCheckNumEntries = smapsDontCheckIndex;
	smapsDontCheckIndex = 0;

	if (debug) for (int i = 0; i < smapsDontCheckNumEntries; i++) {
		fprintf(stderr, "\nSMAP ENTRIES\n");
		fprintf(stderr, "[%d]->start   %p\n", i, smapsDontCheck[i]->start);
		fprintf(stderr, "[%d]->end     %p\n", i, smapsDontCheck[i]->end);
		fprintf(stderr, "[%d]->kb      %u\n", i, smapsDontCheck[i]->kb);
	}
//	fclose(smaps_infile);
}

SMapEntry* newSMapEntry() {
		SMapEntry* chaka;
		chaka = (SMapEntry*)myMalloc(sizeof(SMapEntry));
		chaka->start = 0;
		chaka->end = 0;
		chaka->kb = 0;
		return chaka;
}

void setupSignalHandler() {
		struct sigaction sa;
		sa.sa_flags = SA_SIGINFO;
		sigemptyset(&sa.sa_mask);
		sigaddset(&sa.sa_mask, SIGIO);
		sa.sa_sigaction = sampleMemoryOverhead;
		if (sigaction(SIGRTMIN, &sa, NULL) == -1) {
				perror("sigaction(SIGRTMIN, &sa, NULL) failed.");
				abort();
		}
}

void startSignalTimer() {
		struct sigevent event;
		event.sigev_notify = SIGEV_THREAD_ID;
		event._sigev_un._tid = gettid();
		event.sigev_signo = SIGRTMIN;

		if (timer_create(CLOCK_MONOTONIC, &event, &smap_timer) == -1) {
				perror("timer_create failed");
				abort();
		}

		if (timer_settime(smap_timer, 0, &resumeTimer, NULL) == -1) {
				perror("timer_settime failed");
				abort();
		}
}

pid_t gettid() {
    return syscall(__NR_gettid);
}

void updateGlobalFriendlinessData() {
		friendly_data * thrFriendlyData = &thrData.friendlyData;

		__atomic_add_fetch(&globalFriendlyData.numAccesses, thrFriendlyData->numAccesses, __ATOMIC_SEQ_CST);
		__atomic_add_fetch(&globalFriendlyData.numCacheWrites, thrFriendlyData->numCacheWrites, __ATOMIC_SEQ_CST);
		__atomic_add_fetch(&globalFriendlyData.numCacheOwnerConflicts, thrFriendlyData->numCacheOwnerConflicts, __ATOMIC_SEQ_CST);
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
				fprintf(outfd, "tid %d : num cache owner conflicts  = %ld\n", tid, thrFriendlyData->numCacheOwnerConflicts);
				fprintf(outfd, "tid %d : avg. cache util = %0.4f\n", tid, avgCacheUtil);
				fprintf(outfd, "tid %d : avg. page util  = %0.4f\n", tid, avgPageUtil);
		}
		#endif // END DEBUG BLOCK
}

void calcAppFriendliness() {
		// Final call to update the global data, using this (the main thread's) local data.
		updateGlobalFriendlinessData();

		unsigned long totalAccesses = __atomic_load_n(&globalFriendlyData.numAccesses, __ATOMIC_SEQ_CST);
		unsigned long totalCacheWrites = __atomic_load_n(&globalFriendlyData.numCacheWrites, __ATOMIC_SEQ_CST);
		unsigned long totalCacheOwnerConflicts = __atomic_load_n(&globalFriendlyData.numCacheOwnerConflicts, __ATOMIC_SEQ_CST);
		unsigned long totalCacheBytes = __atomic_load_n(&globalFriendlyData.numCacheBytes, __ATOMIC_SEQ_CST);
		unsigned long totalPageBytes = __atomic_load_n(&globalFriendlyData.numPageBytes, __ATOMIC_SEQ_CST);

		double avgTotalCacheUtil = (double) totalCacheBytes / (totalAccesses * CACHELINE_SIZE);
		double avgTotalPageUtil = (double) totalPageBytes / (totalAccesses * PAGESIZE);
		FILE * outfd = thrData.output;
		//FILE * outfd = stderr;
		fprintf(outfd, "num sampled accesses       = %ld\n", totalAccesses);
		fprintf(outfd, "total cache bytes accessed = %ld\n", totalCacheBytes);
		fprintf(outfd, "total page bytes accessed  = %ld\n", totalPageBytes);
		fprintf(outfd, "cache line writes          = %ld\n", totalCacheWrites);
		fprintf(outfd, "cache owner conflicts      = %ld (%7.4lf%%)\n", totalCacheOwnerConflicts, (totalCacheOwnerConflicts / safeDivisor(totalCacheWrites)));
		fprintf(outfd, "avg. cache utilization     = %6.3f%%\n", (avgTotalCacheUtil * 100.0));
		fprintf(outfd, "avg. page utilization      = %6.3f%%\n", (avgTotalPageUtil * 100.0));
}

const char * LockTypeToString(LockType type) {
		switch(type) {
				case MUTEX:
					return "mutex";
					break;
				case SPINLOCK:
					return "spinlock";
					break;
				case TRYLOCK:
					return "trylock";
					break;
				case SPIN_TRYLOCK:
					return "spin_trylock";
					break;
				default:
					return "unknown";
		}
}
