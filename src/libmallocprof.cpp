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
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "libmallocprof.h"
#include "real.hh"
#include "selfmap.hh"
#include "shadowmemory.hh"
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
extern char __executable_start;
float memEfficiency = 0;
initStatus profilerInitialized = NOT_INITIALIZED;
pid_t pid;
size_t alignment = 0;
size_t malloc_mmap_threshold = 0;
size_t metadata_object = 0;
size_t metadata_overhead = 0;
size_t total_blowup = 0;
size_t totalSizeAlloc = 0;
size_t totalSizeFree = 0;
size_t totalSizeDiff = 0;
size_t totalMemOverhead = 0;
uint64_t min_pos_callsite_id;

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
unsigned timer_nsec = 333000000;
unsigned timer_sec = 0;
//Smaps Sampling-------------//

//Array of class sizes
int num_class_sizes;
size_t* class_sizes;

//Debugging flags DEBUG
const bool d_malloc_info = false;
const bool d_mprotect = false;
const bool d_mmap = false;
const bool d_class_sizes = false;
const bool d_pmuData = false;
const bool d_write_tad = true;
const bool d_readFile = false;
const bool d_initLocal = false;
const bool d_initGlobal = false;
const bool d_getBlowup = false;
const bool d_updateCounters = false;
const bool d_myLocalMalloc = false;
const bool d_initMyLocalBuffer = false;
const bool d_checkSizes = false;
const bool d_myMemUsage = false;
const bool d_dumpHashmaps = false;
const bool d_trace = false;

//Atomic Globals ATOMIC
std::atomic_bool mmap_active(false);
std::atomic_bool sbrk_active(false);
std::atomic_bool madvise_active(false);
std::atomic<std::size_t> freed_bytes (0);
std::atomic<std::size_t> blowup_bytes (0);
std::atomic<std::size_t> alignment_bytes (0);

thread_local uint64_t total_time_wait = 0;

thread_local uint num_sbrk = 0;
thread_local uint num_madvise = 0;
thread_local uint malloc_mmaps = 0;
// thread_local uint total_mmaps = 0;

thread_local uint size_sbrk = 0;
thread_local uint blowup_allocations = 0;
thread_local uint64_t cycles_free = 0;
thread_local uint64_t cycles_new = 0;
thread_local uint64_t cycles_reused = 0;


/// REQUIRED!
std::atomic<unsigned>* globalFreeArray = nullptr;

//Thread local variables THREAD_LOCAL
__thread thread_data thrData;
thread_local bool waiting;
thread_local bool inAllocation;
thread_local bool inDeallocation;
thread_local bool inMmap;
thread_local bool samplingInit = false;
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

spinlock globalize_lck;

// Pre-init private allocator memory
char myMem[TEMP_MEM_SIZE];
void* myMemEnd;
uint64_t myMemPosition = 0;
uint64_t myMemAllocations = 0;
spinlock myMemLock;

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
	inline void getCallsites(void **callsites);

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

	if (d_myMemUsage) fprintf(stderr, "Entering constructor..\n");
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

	// Allocate and initialize all shadow memory-related mappings
	ShadowMemory::initialize();

	RealX::initializer();

	myMemEnd = (void*) (myMem + TEMP_MEM_SIZE);
	myMemLock.init();
	globalize_lck.init();
	pid = getpid();

	// Calculate the minimum possible callsite ID by taking the low four bytes
	// of the start of the program text and repeating them twice, back-to-back,
	// resulting in eight bytes which resemble: 0x<lowWord><lowWord>
	uint64_t btext = (uint64_t)&__executable_start & 0xFFFFFFFF;
	min_pos_callsite_id = (btext << 32) | btext;

	// addressUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	lockUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 128);
	overhead.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 256);
	//threadContention.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
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

	// After this, we have the filename we need to read from
	// in the form of "allocator.info" in allocatorFileName
	allocator_name = strrchr(allocator_name, '/') + 1;
	char* period = strchr(allocator_name, '.');
	uint64_t bytes = (uint64_t)period - (uint64_t)allocator_name;
	size_t extensionBytes = 6;
	allocatorFileName = (char*) myMalloc (bytes+extensionBytes);
	memcpy (allocatorFileName, allocator_name, bytes);
	snprintf (allocatorFileName+bytes, extensionBytes, ".info");
	//myFree(allocator_name);

	// Load info from allocatorInfoFile
	readAllocatorFile();

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

	fprintf(thrData.output, ">>> stack start @ %p, stack end @ %p\n",
			thrData.stackStart, thrData.stackEnd);
	fprintf(thrData.output, ">>> program break @ %p\n", program_break);
	fflush(thrData.output);

	if (bibop) {
		//Print class sizes to thrData output
		fprintf (thrData.output, ">>> class_sizes ");

		//Create and init Overhead for each class size
		for (int i = 0; i < num_class_sizes; i++) {

			size_t cs = class_sizes[i];

			if (d_class_sizes) printf ("hashmap entry %d key is %zu\n", i, cs);
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
	if (d_myMemUsage) {
		printMyMemUtilization();
		fprintf(stderr, "Leaving constructor..\n\n");
	}

	smaps_buffer = (char*) myMalloc(smaps_bufferSize);
	sprintf(smaps_fileName, "/proc/%d/smaps", pid);
	start_smaps();

	resumeTimer.it_value.tv_sec = timer_sec;
	resumeTimer.it_value.tv_nsec = timer_nsec;
	resumeTimer.it_interval.tv_sec = timer_sec;
	resumeTimer.it_interval.tv_nsec = timer_nsec;

	stopTimer.it_value.tv_sec = 0;
	stopTimer.it_value.tv_nsec = 0;
	stopTimer.it_interval.tv_sec = 0;
	stopTimer.it_interval.tv_nsec = 0;

	worstCaseOverhead.efficiency = 100.00;
	setupSignalHandler();
	startSignalTimer();

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
	//    fprintf(stderr, "\nEntering exitHandler\n");

	inRealMain = false;
	#ifndef NO_PMU
	doPerfRead();
	#endif

	if (timer_settime(smap_timer, 0, &stopTimer, NULL) == -1) {
			perror("timer_settime failed");
			abort();
	}
	if (timer_delete(smap_timer) == -1) {
			perror("timer_delete failed");
	}

	//    calculateMemOverhead();
	if(thrData.output) {
			fflush(thrData.output);
	}
	writeAllocData();
	// writeContention();
	if(thrData.output) {
		fclose(thrData.output);
	}

	//    if (smap_samples != 0) {
	//            uint64_t avg = (smap_sample_cycles / smap_samples);
	//            fprintf(stderr, "smap_samples: %lu, avg cycles: %lu\n", smap_samples, avg);
	//    }
	//    else {
	//            fprintf(stderr, "smap_samples was 0\n");
	//    }

	fprintf(stderr, "---WorstCaseOverhead---\n"
					"PhysicalMem:          %lu\n"
					"Alignment:            %lu\n"
					"Blowup:               %lu\n"
					"Efficiency:           %.4f\n"
					"-----------------------\n",
					worstCaseOverhead.kb,
					worstCaseOverhead.alignment,
					worstCaseOverhead.blowup,
					worstCaseOverhead.efficiency);
}

// MallocProf's main function
int libmallocprof_main(int argc, char ** argv, char ** envp) {
	// Register our cleanup routine as an on-exit handler.
	atexit(exitHandler);

	#ifndef NO_PMU
	// If the PMU sampler has not yet been set up for this thread, set it up now
	if(!samplingInit){
		initSampling();
		samplingInit = true;
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
		if((profilerInitialized != INITIALIZED) || (!selfmapInitialized)) {
			return myMalloc (sz);
		}

		//Malloc is being called by a thread that is already in malloc
		if (inAllocation) return RealX::malloc (sz);

		if (!inRealMain) return RealX::malloc(sz);

		//thread_local
		inAllocation = true;

		#ifndef NO_PMU
		// If the PMU sampler has not yet been set up for this thread, set it up now
		if(!samplingInit){
			initSampling();
			samplingInit = true;
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

		//Do after
		current_tc->totalMemoryUsage += PAGESIZE * ShadowMemory::updateObject((void *)allocData.address, allocData.size, false);
    if(current_tc->totalMemoryUsage > current_tc->maxTotalMemoryUsage) {
      current_tc->maxTotalMemoryUsage = current_tc->totalMemoryUsage;
    }
		doAfter(&allocData);

		allocData.cycles = allocData.tsc_after - allocData.tsc_before;

		//            addressUsage.insertIfAbsent(allocData.address, newObjectTuple(allocData.address, sz));
		// Gets overhead, address usage, mmap usage, memHWM, and prefInfo
		analyzeAllocation(&allocData);

    incrementMemoryUsage(sz);

		// thread_local
		inAllocation = false;

		return object;
	}

	void * yycalloc(size_t nelem, size_t elsize) {

		if (profilerInitialized != INITIALIZED) {

			void * ptr = NULL;
			ptr = yymalloc (nelem * elsize);
			if (ptr) memset(ptr, 0, nelem * elsize);
			return ptr;
		}

		if (inAllocation) return RealX::calloc(nelem, elsize);

		if (!inRealMain) return RealX::calloc (nelem, elsize);

		// if the PMU sampler has not yet been set up for this thread, set it up now
		if(!samplingInit){
			initSampling();
			samplingInit = true;
		}

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

		// Do after
		current_tc->totalMemoryUsage += PAGESIZE * ShadowMemory::updateObject((void *)allocData.address, allocData.size, false);
    if(current_tc->totalMemoryUsage > current_tc->maxTotalMemoryUsage) {
      current_tc->maxTotalMemoryUsage = current_tc->totalMemoryUsage;
    }
		doAfter(&allocData);

		allocData.cycles = allocData.tsc_after - allocData.tsc_before;

		// Gets overhead, address usage, mmap usage, memHWM, and perfInfo
		analyzeAllocation(&allocData);

    incrementMemoryUsage(nelem * elsize);

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
		if(!samplingInit){
			initSampling();
			samplingInit = true;
		}

		//thread_local
		inAllocation = true;

		if (!localFreeArrayInitialized) {
			initLocalFreeArray();
		}

		//Data we need for each free
		allocation_metadata allocData = init_allocation(0, FREE);
		allocData.address = reinterpret_cast <uint64_t> (ptr);

		//fprintf(stderr, "free(%p), before doBefore, allocData.address = %#lx\n", ptr, allocData.address);
		//Do before free
		doBefore(&allocData);

    decrementMemoryUsage(ptr);
		#warning Note for Hongyu: we dont need this here in free, do we? return value from updateObject should be 0?
		//current_tc->totalMemoryUsage += PAGESIZE * ShadowMemory::updateObject((void *)allocData.address, allocData.size);
		ShadowMemory::updateObject((void *)allocData.address, allocData.size, true);

		//Do free
		RealX::free(ptr);

		//Do after free
		doAfter(&allocData);

		//Update free counters
		allocData.classSize = updateFreeCounters(allocData.address);

    //thread_local
    inAllocation = false;

		//Update atomics
		cycles_free += (allocData.tsc_after - allocData.tsc_before);

		localTAD.numDeallocationFaults += allocData.after.faults - allocData.before.faults;
		localTAD.numDeallocationTlbReadMisses += allocData.after.tlb_read_misses - allocData.before.tlb_read_misses;
		localTAD.numDeallocationTlbWriteMisses += allocData.after.tlb_write_misses - allocData.before.tlb_write_misses;
		localTAD.numDeallocationCacheMisses += allocData.after.cache_misses - allocData.before.cache_misses;
		localTAD.numDeallocationInstrs += allocData.after.instructions - allocData.before.instructions;

		if (((allocData.after.instructions - allocData.before.instructions) != 0) && d_pmuData){
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
	}

	void * yyrealloc(void * ptr, size_t sz) {

		if (!realInitialized) RealX::initializer();
		if (profilerInitialized != INITIALIZED) {
			if(ptr == NULL) return yymalloc(sz);
			yyfree(ptr);
			return yymalloc(sz);
		}

		if (!mapsInitialized) return RealX::realloc (ptr, sz);
		if (inAllocation) return RealX::realloc (ptr, sz);
		if (!inRealMain) return RealX::realloc (ptr, sz);

		//if the PMU sampler has not yet been set up for this thread, set it up now
		if(!samplingInit){
			initSampling();
			samplingInit = true;
		}

		//Data we need for each allocation
		allocation_metadata allocData = init_allocation(sz, REALLOC);

		// allocated object
		void* object;

		//thread_local
		inAllocation = true;

		if (!localFreeArrayInitialized) {
			initLocalFreeArray();
		}

		//Do before
		// doBefore(&before, &tsc_before);
		doBefore(&allocData);

		//Do allocation
		object = RealX::realloc(ptr, sz);
		allocData.address = reinterpret_cast <uint64_t> (object);

		//Do after
		#warning must implement realloc-specific behavior for shadow memory updating
		current_tc->totalMemoryUsage += PAGESIZE * ShadowMemory::updateObject((void *)allocData.address, allocData.size, false);
    if(current_tc->totalMemoryUsage > current_tc->maxTotalMemoryUsage) {
      current_tc->maxTotalMemoryUsage = current_tc->totalMemoryUsage;
    }
		doAfter(&allocData);

		// cyclesForRealloc = tsc_after - tsc_before;
		allocData.cycles = allocData.tsc_after - allocData.tsc_before;

		//Gets overhead, address usage, mmap usage
		// analyzeAllocation(sz, address, cyclesForRealloc, classSize, &reused);
		analyzeAllocation(&allocData);

    if(object != ptr)
      incrementMemoryUsage(sz);

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
		logUnsupportedOp();
		return -1;
	}
	void * yyaligned_alloc(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * yymemalign(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * yypvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
	}

	inline void getCallsites(void **callsites) {
		int i = 0;
		void * btext = &__executable_start;
		void * etext = &data_start;

		// Fetch the frame address of the topmost stack frame
		struct stack_frame * current_frame =
			(struct stack_frame *)(__builtin_frame_address(0));

		// Initialize the prev_frame pointer to equal the current_frame. This
		// simply ensures that the while loop below will be entered and
		// executed and least once
		struct stack_frame * prev_frame = current_frame;

		// Initialize the array elements
		callsites[0] = (void *)NULL;
		callsites[1] = (void *)NULL;

		// Loop condition tests the validity of the frame address given for the
		// previous frame by ensuring it actually points to a location located
		// on the stack
		while((i < 2) && ((void *)prev_frame <= (void *)thrData.stackStart) &&
				(prev_frame >= current_frame)) {
			// Inspect the return address belonging to the previous stack frame;
			// if it's located in the program text, record it as the next
			// callsite
			void * caller_addr = prev_frame->caller_address;
			if((caller_addr >= btext) && (caller_addr <= etext)) {
				callsites[i++] = caller_addr;
			}
			// Walk the prev_frame pointer backward in preparation for the
			// next iteration of the loop
			prev_frame = prev_frame->prev;
		}
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

void operator delete (void * ptr) __THROW {
	yyfree (ptr);
}

void * operator new[] (size_t size) {
	return yymalloc(size);
}

void operator delete[] (void * ptr) __THROW {
	yyfree (ptr);
}

void analyzeAllocation(allocation_metadata *metadata) {

	//Analyzes for alignment and blowup
	getOverhead(metadata->size, metadata->address, metadata->classSize, &(metadata->reused));

	//Analyze address usage
	#warning disabled call to getAddressUsage
	//getAddressUsage(metadata->size, metadata->address, metadata->cycles);

	//Update mmap region info
	#warning disabled call to getMappingsUsage
	//getMappingsUsage(metadata->size, metadata->address, metadata->classSize);

	// Analyze perfinfo
	analyzePerfInfo(metadata);
}

size_t updateFreeCounters(uint64_t address) {

	size_t size = 0;
	size_t current_class_size = 0;

	if (bibop){
			size = ShadowMemory::getObjectSize((void*)address);

			current_class_size = getClassSizeFor(size);

			//Increase the free object counter for this class size
			//GetSizeIndex returns class size for bibop
			//0 or 1 for bump pointer / 0 for small, 1 for large objects
			if (size <= malloc_mmap_threshold) {
					short class_size_index = getClassSizeIndex(size);
					localFreeArray[class_size_index]++;
					globalFreeArray[class_size_index]++;
					if (d_updateCounters) printf ("globalFreeArray[%d]++\n", class_size_index);
			}
	}

	else {freed_bytes += size;}
	return current_class_size;
}

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
void getAddressUsage(size_t size, uint64_t address, uint64_t cycles) {

	ObjectTuple* t; //Has this address been used before
	if (addressUsage.find (address, &t)) {
		t->szUsed = size;
		cycles_reused += cycles;
	} else {
		cycles_new += cycles;
		addressUsage.insert (address, newObjectTuple(address, size));
	}
}
*/

void getMappingsUsage(size_t size, uint64_t address, size_t classSize) {

	for (auto entry : mappings) {
		auto data = entry.getData();
		if (data->start <= address && address <= data->end) {
			data->allocations.fetch_add(1, relaxed);
			break;
		}
	}
}

void getOverhead (size_t size, uint64_t address, size_t classSize, bool* reused) {

	//If size is greater than malloc_mmap_threshold
	//Then it will be mmap'd. So no need to check
	if (size <= malloc_mmap_threshold) {

		//Check for classSize alignment bytes
		// if (bibop) getAlignment(size, classSize);
		getAlignment(size, classSize);

		//Check for memory blowup
		getBlowup(size, classSize, reused);
	}
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
			if (d_getBlowup) printf ("I have free. globalFreeArray[%d]--\n", class_size_index);
			return;
		}

		//I don't have free objects on my local list, check global
		else if (globalFreeArray[class_size_index] > 0) {

			//I didn't have free, but someone else did
			//Log this as blowup. Don't return

			globalFreeArray[class_size_index]--;
			if (d_getBlowup) printf ("I don't have free. globalFreeArray[%d]--\n", class_size_index);
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
	}
}

//Return the appropriate class size that this object should be in
size_t getClassSizeFor (size_t size) {

	if(size > malloc_mmap_threshold) {
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

ThreadContention* newThreadContention (uint64_t threadID) {

	ThreadContention* tc = (ThreadContention*) myMalloc (sizeof (ThreadContention));
	tc->tid = threadID;
	return tc;
}

LC* newLC () {
	LC* lc = (LC*) myMalloc(sizeof(LC));
	lc->contention = 1;
	lc->maxContention = 1;
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
	if((myMemPosition + size) < TEMP_MEM_SIZE) {
		p = (void *)(myMem + myMemPosition);
		myMemPosition += size;
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
	globalTAD.numAllocationCacheRefs += localTAD.numAllocationCacheRefs;
	globalTAD.numAllocationInstrs += localTAD.numAllocationInstrs;

	globalTAD.numAllocationFaultsFFL += localTAD.numAllocationFaultsFFL;
	globalTAD.numAllocationTlbReadMissesFFL += localTAD.numAllocationTlbReadMissesFFL;
	globalTAD.numAllocationTlbWriteMissesFFL += localTAD.numAllocationTlbWriteMissesFFL;
	globalTAD.numAllocationCacheMissesFFL += localTAD.numAllocationCacheMissesFFL;
	globalTAD.numAllocationCacheRefsFFL += localTAD.numAllocationCacheRefsFFL;
	globalTAD.numAllocationInstrsFFL += localTAD.numAllocationInstrsFFL;

	globalTAD.numDeallocationFaults += localTAD.numDeallocationFaults;
	globalTAD.numDeallocationCacheMisses += localTAD.numDeallocationCacheMisses;
	globalTAD.numDeallocationCacheRefs += localTAD.numDeallocationCacheRefs;
	globalTAD.numDeallocationInstrs += localTAD.numDeallocationInstrs;
	globalTAD.numDeallocationTlbReadMisses += localTAD.numDeallocationTlbReadMisses;
	globalTAD.numDeallocationTlbWriteMisses += localTAD.numDeallocationTlbWriteMisses;

	globalTAD.total_time_wait += localTAD.total_time_wait;
	globalTAD.blowup_bytes += localTAD.blowup_bytes;

	globalTAD.num_sbrk += localTAD.num_sbrk;
	globalTAD.num_madvise += localTAD.num_madvise;
	globalTAD.malloc_mmaps += localTAD.malloc_mmaps;

	globalTAD.size_sbrk += localTAD.size_sbrk;
	globalTAD.blowup_allocations += localTAD.blowup_allocations;
	globalTAD.cycles_free += localTAD.cycles_free;
	globalTAD.cycles_new += localTAD.cycles_new;
	globalTAD.cycles_reused += localTAD.cycles_reused;
	globalize_lck.unlock();
}

void writeAllocData () {
	if (d_trace) fprintf(stderr, "Entering writeAllocData()\n");

	fprintf(thrData.output, ">>> malloc_mmaps             %u\n", malloc_mmaps);
	fprintf(thrData.output, ">>> malloc_mmap_threshold    %zu\n", malloc_mmap_threshold);
	fprintf(thrData.output, ">>> num_madvise              %u\n", num_madvise);
	fprintf(thrData.output, ">>> num_sbrk                 %u\n", num_sbrk);
	fprintf(thrData.output, ">>> size_sbrk                %u\n", size_sbrk);
	fprintf(thrData.output, ">>> alignment                %zu\n", alignment);

	// writeOverhead();

	// writeMappings();

	if (d_write_tad) writeThreadMaps();
	writeThreadContention();

	fflush (thrData.output);
}

void writeThreadMaps () {
	fprintf (thrData.output, "\n>>>>> NEW ALLOCATIONS <<<<<\n");
	fprintf (thrData.output, "allocation faults               %lu\n", globalTAD.numAllocationFaults);
	fprintf (thrData.output, "allocation tlb read misses      %lu\n", globalTAD.numAllocationTlbReadMisses);
	fprintf (thrData.output, "allocation tlb write misses     %lu\n", globalTAD.numAllocationTlbWriteMisses);
	fprintf (thrData.output, "allocation cache misses         %lu\n", globalTAD.numAllocationCacheMisses);
	fprintf (thrData.output, "allocation cache refs           %lu\n", globalTAD.numAllocationCacheRefs);
	fprintf (thrData.output, "num allocation instr            %lu\n", globalTAD.numAllocationInstrs);
	fprintf (thrData.output, "\n");

	fprintf (thrData.output, "\n>>>>> FREELIST ALLOCATIONS <<<<<\n");
	fprintf (thrData.output, "allocation faults               %lu\n",   globalTAD.numAllocationFaultsFFL);
	fprintf (thrData.output, "allocation tlb read misses      %lu\n",   globalTAD.numAllocationTlbReadMissesFFL);
	fprintf (thrData.output, "allocation tlb write misses     %lu\n",   globalTAD.numAllocationTlbWriteMissesFFL);
	fprintf (thrData.output, "allocation cache misses         %lu\n",   globalTAD.numAllocationCacheMissesFFL);
	fprintf (thrData.output, "allocation cache refs           %lu\n",   globalTAD.numAllocationCacheRefsFFL);
	fprintf (thrData.output, "num allocation instr            %lu\n",   globalTAD.numAllocationInstrsFFL);
	fprintf (thrData.output, "\n");

	fprintf (thrData.output, "\n>>>>> DEALLOCATIONS <<<<<\n");
	fprintf (thrData.output, "deallocation faults             %lu\n", globalTAD.numDeallocationFaults);
	fprintf (thrData.output, "deallocation cache misses       %lu\n", globalTAD.numDeallocationCacheMisses);
	fprintf (thrData.output, "deallocation cache refs         %lu\n", globalTAD.numDeallocationCacheRefs);
	fprintf (thrData.output, "num deallocation instr          %lu\n", globalTAD.numDeallocationInstrs);
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

	if (d_trace) fprintf(stderr, "Entering writeContention()\n");
	fprintf (thrData.output, "\n------------lock usage------------\n");
	for (auto lock : lockUsage)
		fprintf (thrData.output, "lockAddr= %#lx  maxContention= %d\n",
				lock.getKey(), lock.getData()->maxContention);

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
		alignment += data->getAlignment();
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
	getPerfInfo(&(metadata->before));
	metadata->tsc_before = rdtscp();
}

void doAfter (allocation_metadata *metadata) {
	getPerfInfo(&(metadata->after));
	metadata->tsc_after = rdtscp();
}

void incrementMemoryUsage(size_t size) {
  size_t classSize = 0;
  if(bibop) {
    classSize = getClassSizeFor(size);
  } else {
    classSize = ShadowMemory::libc_malloc_usable_size(size);
  }

	current_tc->realAllocatedMemoryUsage += classSize;
	if(current_tc->realAllocatedMemoryUsage > current_tc->maxRealAllocatedMemoryUsage) {
    current_tc->maxRealAllocatedMemoryUsage = current_tc->realAllocatedMemoryUsage;
  }
	current_tc->realMemoryUsage += size;
	if(current_tc->realMemoryUsage > current_tc->maxRealMemoryUsage) {
    current_tc->maxRealMemoryUsage = current_tc->realMemoryUsage;
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

	current_tc->realAllocatedMemoryUsage -= classSize;
	current_tc->realMemoryUsage -= ShadowMemory::getObjectSize(addr);
}

void collectAllocMetaData(allocation_metadata *metadata) {
	if (!metadata->reused){
		localTAD.numAllocationFaults += metadata->after.faults - metadata->before.faults;
		localTAD.numAllocationTlbReadMisses += metadata->after.tlb_read_misses - metadata->before.tlb_read_misses;
		localTAD.numAllocationTlbWriteMisses += metadata->after.tlb_write_misses - metadata->before.tlb_write_misses;
		localTAD.numAllocationCacheMisses += metadata->after.cache_misses - metadata->before.cache_misses;
		localTAD.numAllocationInstrs += metadata->after.instructions - metadata->before.instructions;
	} else {
		localTAD.numAllocationFaultsFFL += metadata->after.faults - metadata->before.faults;
		localTAD.numAllocationTlbReadMissesFFL += metadata->after.tlb_read_misses - metadata->before.tlb_read_misses;
		localTAD.numAllocationTlbWriteMissesFFL += metadata->after.tlb_write_misses - metadata->before.tlb_write_misses;
		localTAD.numAllocationCacheMissesFFL += metadata->after.cache_misses - metadata->before.cache_misses;
		localTAD.numAllocationInstrsFFL += metadata->after.instructions - metadata->before.instructions;
	}
}

void analyzePerfInfo(allocation_metadata *metadata) {
	collectAllocMetaData(metadata);

	if (metadata->after.instructions - metadata->before.instructions != 0 && d_pmuData){
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
}

void readAllocatorFile() {

	size_t bufferSize = 1024;
	FILE* infile = nullptr;
	char* buffer = (char*) myMalloc(bufferSize);
	char* token;

	if ((infile = fopen (allocatorFileName, "r")) == NULL) {
		perror("Failed to open allocator info file. Make sure to run the prerun lib and"
				"\nthe file (i.e. allocator.info) is in this directory. Quit");
		abort();
	}

	while ((getline(&buffer, &bufferSize, infile)) > 0) {

		if (d_readFile) printf ("buffer= %s\n", buffer);

		token = strtok(buffer, " ");
		if (d_readFile) printf ("token= %s\n", token);

		if ((strcmp(token, "style")) == 0) {
			if (d_readFile) printf ("token matched style\n");

			token = strtok(NULL, " ");

			if (d_readFile) printf ("styleToken= %s\n", token);

			if ((strcmp(token, "bibop\n")) == 0) {
				if (d_readFile) printf ("styleToken matched bibop\n");
				bibop = true;
			}
			else {
				bumpPointer = true;
				if (d_readFile) printf ("styleToken matched bump_pointer\n");
			}
			continue;
		}

		else if ((strcmp(token, "class_sizes")) == 0) {
			if (d_readFile) printf ("token matched class_sizes\n");

			token = strtok(NULL, " ");
			num_class_sizes = atoi(token);
			if (d_readFile) printf ("num_class_sizes= %d\n", num_class_sizes);

			class_sizes = (size_t*) myMalloc (num_class_sizes*sizeof(size_t));

			for (int i = 0; i < num_class_sizes; i++) {
				token = strtok(NULL, " ");
				class_sizes[i] = (size_t) atoi(token);
				if (d_readFile) printf ("class_size[%d]= %zu\n", i, class_sizes[i]);
			}
			continue;
		}

		else if ((strcmp(token, "malloc_mmap_threshold")) == 0) {
			if (d_readFile) printf ("token matched malloc_mmap_threshold\n");
			token = strtok(NULL, " ");
			malloc_mmap_threshold = (size_t) atoi(token);
			if (d_readFile) printf ("malloc_mmap_threshold= %zu\n", malloc_mmap_threshold);
			continue;
		}

		else if ((strcmp(token, "metadata_object")) == 0) {
			if (d_readFile) printf ("token matched metadata_object\n");
			token = strtok(NULL, " ");
			metadata_object = (size_t) atoi(token);
			if (d_readFile) printf ("metadata_object= %zu\n", metadata_object);
			continue;
		}
		if (d_readFile) printf ("\n");
	}

	myFree(buffer);
	myFree(allocatorFileName);
}

void initLocalFreeArray () {
	if (bibop) {
		localFreeArray = (unsigned*) myMalloc(num_class_sizes*sizeof(unsigned));
	}

	else {
		localFreeArray = (unsigned*) myMalloc(2*sizeof(unsigned));
	}
	if (d_initLocal) printf ("tid %zu - initLocalFreeArray\n", pthread_self());
	localFreeArrayInitialized = true;
}

void initGlobalFreeArray () {
	if (bibop) {
		globalFreeArray = (std::atomic<unsigned>*) myMalloc(num_class_sizes*sizeof(unsigned));
	}

	else {
		globalFreeArray = (std::atomic<unsigned>*) myMalloc(2*sizeof(unsigned));
	}
	if (d_initGlobal) printf ("initGlobalFreeArray\n");
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
	if (d_myLocalMalloc) fprintf (stderr, "Thread %lu: new local allocation. Total %lu\n", myThreadID, myLocalAllocations);
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
	if (d_initMyLocalBuffer) fprintf (stderr, "Thread %lu localBuffer address is %p\n", myThreadID, myLocalMem);
	myLocalMemEnd = (void*) ((char*)myLocalMem + LOCAL_BUF_SIZE);
	myLocalPosition = 0;
	myLocalMemInitialized = true;
}

void sampleMemoryOverhead(int i, siginfo_t* s, void* p) {

		size_t alignBytes = alignment_bytes.load();
		size_t blowupBytes = blowup_bytes.load();

		if (timer_settime(smap_timer, 0, &stopTimer, NULL) == -1) {
				perror("timer_settime failed");
				abort();
		}

		//    uint64_t startTime = rdtscp();
		smap_samples++;
		int numSmapEntries = 0;
		int numSkips = 0;
		int numHeapMatches = 0;
		bool dontCheck = false;
		void* start;
		void* end;
		unsigned kb = 0;
		smapsDontCheckIndex = 0;

		currentCaseOverhead.kb = 0;
		currentCaseOverhead.efficiency = 0;

		if ((smaps_infile = fopen (smaps_fileName, "r")) == NULL) {
				fprintf(stderr, "failed to open smaps\n");
				return;
		}

		while ((getline(&smaps_buffer, &smaps_bufferSize, smaps_infile)) > 0) {

				dontCheck = false;
				numSmapEntries++;

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
		fclose(smaps_infile);
		//    uint64_t endTime = rdtscp();

		//    smap_sample_cycles += (endTime - startTime);
		//    fprintf(stderr, "leaving sample, %d smap entries\n"
		//                                    "bytes= %lu, blowup= %zu, alignment= %zu\n"
		//                                    "skips= %d, hits= %d, cycles: %lu\nefficiency= %.5f\n",
		//                                    numSmapEntries, bytes, blowupBytes, alignBytes,
		//                                    numSkips, numHeapMatches, (endTime-startTime), e);

		if (timer_settime(smap_timer, 0, &resumeTimer, NULL) == -1) {
				perror("timer_settime failed");
				abort();
		}
		//    getchar();
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
				//            getchar();
		}

		smapsDontCheckNumEntries = smapsDontCheckIndex;
		smapsDontCheckIndex = 0;

		if (debug) for (int i = 0; i < smapsDontCheckNumEntries; i++) {
				fprintf(stderr, "\nSMAP ENTRIES\n");
				fprintf(stderr, "[%d]->start   %p\n", i, smapsDontCheck[i]->start);
				fprintf(stderr, "[%d]->end     %p\n", i, smapsDontCheck[i]->end);
				fprintf(stderr, "[%d]->kb      %u\n", i, smapsDontCheck[i]->kb);
		}
		fclose(smaps_infile);
		//    fprintf(stderr, "numDontCheck %d\n", smapsDontCheckNumEntries);

		//    getchar();
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

