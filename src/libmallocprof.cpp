/**
 * @file libmallocprof.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 * @author Sam Silvestro <sam.silvestro@utsa.edu>
 * @author Richard Salcedo <kbgagt@gmail.com>
 */

#include <atomic>  //atomic vars
#include <dlfcn.h> //dlsym
#include <fcntl.h> //fopen flags
#include <stdio.h> //print, getline
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "libmallocprof.h"
#include "real.hh"
#include "spinlock.hh"
#include "selfmap.hh"
#include "xthreadx.hh"

//Globals
bool bibop = false;
bool bumpPointer = false;
bool inRealMain = false;
bool mapsInitialized = false;
bool opening_maps_file = false;
bool realInitialized = false;
bool selfmapInitialized = false;
std::atomic<bool> creatingThread (false);
bool inConstructor = false;
char* allocator_name;
char* allocatorFileName;
extern char data_start;
extern char _etext;
extern char * program_invocation_name;
extern void * __libc_stack_end;
extern char __executable_start;
float memEfficiency = 0;
initStatus profilerInitialized = NOT_INITIALIZED;
pid_t pid;
size_t alignment = 0;
size_t blowup_bytes = 0;
size_t malloc_mmap_threshold = 0;
size_t metadata_object = 0;
size_t metadata_overhead = 0;
size_t totalSizeAlloc = 0;
size_t totalSizeFree = 0;
size_t totalSizeDiff = 0;
size_t totalMemOverhead = 0;
uint64_t min_pos_callsite_id;

//Array of class sizes
int num_class_sizes;
size_t* class_sizes;

//Debugging flags DEBUG
const bool d_malloc_info = false;
const bool d_mprotect = false;
const bool d_mmap = false;
const bool d_class_sizes = false;
const bool d_pmuData = false;
const bool d_write_mappings = true;
const bool d_write_tad = true;
const bool d_readFile = false;
const bool d_initLocal = false;
const bool d_initGlobal = false;
const bool d_getBlowup = false;
const bool d_updateCounters = false;
const bool d_myLocalMalloc = false;
const bool d_initMyLocalBuffer = true;
const bool d_checkSizes = false;
const bool d_myMemUsage = true;
const bool d_dumpHashmaps = false;
const bool d_trace = true;

//Atomic Globals ATOMIC
std::atomic_bool mmap_active(false);
std::atomic_bool sbrk_active(false);
std::atomic_bool madvise_active(false);
std::atomic_uint num_free (0);
std::atomic_uint num_malloc (0);
std::atomic_uint num_calloc (0);
std::atomic_uint num_realloc (0);
std::atomic_uint new_address (0);
std::atomic_uint reused_address (0);
std::atomic_uint num_pthread_mutex_locks (0);
std::atomic_uint num_trylock (0);
std::atomic_uint num_dontneed (0);
std::atomic_uint num_madvise (0);
std::atomic_uint num_sbrk (0);
std::atomic_uint size_sbrk (0);
std::atomic_uint malloc_mmaps (0);
std::atomic_uint total_mmaps (0);
std::atomic_uint threads (0);
std::atomic_uint blowup_allocations (0);
std::atomic_uint num_mprotect (0);
std::atomic_uint num_actual_malloc (0);
std::atomic<std::uint64_t> cycles_free (0);
std::atomic<std::uint64_t> cycles_new (0);
std::atomic<std::uint64_t> cycles_reused (0);
std::atomic<std::uint64_t> total_time_wait (0);
std::atomic<std::size_t> totalMemAllocated (0);
std::atomic<unsigned>* globalFreeArray = nullptr;

//Thread local variables THREAD_LOCAL
__thread thread_data thrData;
thread_local bool waiting;
thread_local bool inAllocation;
thread_local bool inMmap;
thread_local bool samplingInit = false;
thread_local uint64_t timeAttempted;
thread_local uint64_t timeWaiting;
thread_local unsigned* localFreeArray = nullptr;
thread_local bool localFreeArrayInitialized = false;
thread_local uint64_t myThreadID;

#ifdef USE_THREAD_LOCAL
thread_local void* myLocalMem;
thread_local void* myLocalMemEnd;
thread_local uint64_t myLocalPosition;
thread_local uint64_t myLocalAllocations;
thread_local bool myLocalMemInitialized = false;
#endif

// Pre-init private allocator memory
char myMem[TEMP_MEM_SIZE];
void* myMemEnd;
uint64_t myMemPosition = 0;
uint64_t myMemAllocations = 0;
spinlock myMemLock;

//Hashmap of class size to TAD*
typedef HashMap <uint64_t, thread_alloc_data*, spinlock> Class_Size_TAD;
//Hashmap of thread ID to Class_Size_TAD
HashMap <uint64_t, Class_Size_TAD*, spinlock> threadToCSM;

//Hashmap of class size to tad struct, for all thread data summed up
//HashMap <uint64_t, thread_alloc_data*, spinlock> allThreadsTadMap;
std::map<uint64_t, thread_alloc_data*> allThreadsTadMap;

//Hashmap of malloc'd addresses to a ObjectTuple
HashMap <uint64_t, ObjectTuple*, spinlock> addressUsage;

//Hashmap of lock addr to LC
HashMap <uint64_t, LC*, spinlock> lockUsage;

//Hashmap of mmap addrs to tuple:
HashMap <uint64_t, MmapTuple*, spinlock> mappings;

//Hashmap of Overhead objects
HashMap <size_t, Overhead*, spinlock> overhead;

//Hashmap of tid to ThreadContention*
HashMap <uint64_t, ThreadContention*, spinlock> threadContention;

typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main_mallocprof;

extern "C" {
	// Function prototypes
	addrinfo addr2line(void * addr);
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

//Constructor
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

	RealX::initializer();

	myMemEnd = (void*) (myMem + TEMP_MEM_SIZE);
	myMemLock.init();
	pid = getpid();

	// Calculate the minimum possible callsite ID by taking the low four bytes
	// of the start of the program text and repeating them twice, back-to-back,
	// resulting in eight bytes which resemble: 0x<lowWord><lowWord>
	uint64_t btext = (uint64_t)&__executable_start & 0xFFFFFFFF;
	min_pos_callsite_id = (btext << 32) | btext;

	threadToCSM.initialize(HashFuncs::hashInt, HashFuncs::compareInt, 128);
	addressUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	lockUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 128);
	overhead.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	threadContention.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	mapsInitialized = true;

	void * program_break = RealX::sbrk(0);
	thrData.tid = syscall(__NR_gettid);
	thrData.stackStart = (char *)__builtin_frame_address(0);
	thrData.stackEnd = (char *)__libc_stack_end;

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
	myFree(allocator_name);

	// Load info from allocatorInfoFile
	readAllocatorFile();

	initGlobalFreeArray();

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
			overhead.insertIfAbsent(cs, newOverhead());
		}

		fprintf (thrData.output, "\n");
		fflush(thrData.output);
	}
	else {
		//Create an entry in the overhead hashmap with key 0
		overhead.insertIfAbsent(BP_OVERHEAD, newOverhead());
	}

	if (d_checkSizes) {
		size_t sizeOT = sizeof(ObjectTuple);
		size_t sizeMT = sizeof(MmapTuple);
		size_t sizeTC = sizeof(ThreadContention);
		size_t sizeLC = sizeof(LC);
		size_t sizeOH = sizeof(Overhead);
		size_t sizeTAD = sizeof(thread_alloc_data);
		size_t sizeCSM = sizeof(Class_Size_TAD);
		fprintf (stderr, "sizeof(ObjectTuple) is %zu\n", sizeOT);
		fprintf (stderr, "sizeof(MmapTuple) is %zu\n", sizeMT);
		fprintf (stderr, "sizeof(ThreadContention) is %zu\n", sizeTC);
		fprintf (stderr, "sizeof(LC) is %zu\n", sizeLC);
		fprintf (stderr, "sizeof(Overhead) is %zu\n", sizeOH);
		fprintf (stderr, "sizeof(thread_alloc_data) is %zu\n", sizeTAD);
		fprintf (stderr, "sizeof(Class_Size_TAD) is %zu\n", sizeCSM);
	}

	profilerInitialized = INITIALIZED;
	if (d_myMemUsage) {
		printMyMemUtilization();
		fprintf(stderr, "Leaving constructor..\n\n");
	}
	inConstructor = false;
	return profilerInitialized;
}

__attribute__((destructor)) void finalizer_mallocprof () {}

void dumpHashmaps() {

	fprintf(stderr, "addressUsage.printUtilization():\n");
	addressUsage.printUtilization();

	fprintf(stderr, "overhead.printUtilization():\n");
	overhead.printUtilization();
	
	fprintf(stderr, "lockUsage.printUtilization():\n");
	lockUsage.printUtilization();

	fprintf(stderr, "threadContention.printUtilization():\n");
	threadContention.printUtilization();

	fprintf(stderr, "threadToCSM.printUtilization():\n");
	threadToCSM.printUtilization();
	fprintf(stderr, "\n");
}

void printMyMemUtilization () {
	
	fprintf(stderr, "&myMem = %p\n", myMem);
	fprintf(stderr, "myMemEnd = %p\n", myMemEnd);
	fprintf(stderr, "myMemPosition = %lu\n", myMemPosition);
	fprintf(stderr, "myMemAllocations = %lu\n", myMemAllocations);
}

void exitHandler() {
	if (d_myMemUsage) fprintf(stderr, "\nEntering exitHandler\n");

	inRealMain = false;
	#ifndef NO_PMU
	doPerfRead();
	#endif
	calculateMemOverhead();
	if(thrData.output) {
		fflush(thrData.output);
	}
	globalizeThreadAllocData();
	writeAllocData();
	writeContention();
	if(thrData.output) {
		fclose(thrData.output);
	}

	if (d_myMemUsage) {
		printMyMemUtilization();
		if (d_dumpHashmaps) {
			dumpHashmaps();
		}
		fprintf(stderr, "Leaving exitHandler..\n\n");
	}
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
	return real_main_mallocprof (argc, argv, envp);
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

		num_actual_malloc.fetch_add(1, relaxed);

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

		//Do after
		doAfter(&allocData);

        allocData.cycles = allocData.tsc_after - allocData.tsc_before;
		allocData.address = (uint64_t) object;

		// Gets overhead, address usage, mmap usage, memHWM, and prefInfo
		analyzeAllocation(&allocData);

		totalMemAllocated.fetch_add(sz, relaxed);

		// thread_local
		inAllocation = false;

        num_malloc.fetch_add(1, relaxed);

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

		num_calloc.fetch_add(1, relaxed);

		// Data we need for each allocation
		allocation_metadata allocData = init_allocation(nelem * elsize, CALLOC);
		void* object;

		// Do before
		doBefore(&allocData);

		// Do allocation
		object = RealX::calloc(nelem, elsize);

		// Do after
		doAfter(&allocData);

        allocData.cycles = allocData.tsc_after - allocData.tsc_before;
		allocData.address = reinterpret_cast <uint64_t> (object);

		// Gets overhead, address usage, mmap usage, memHWM, and perfInfo
		analyzeAllocation(&allocData);

		totalMemAllocated.fetch_add(nelem*elsize, relaxed);

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

		if (!localFreeArrayInitialized) {
			initLocalFreeArray();
		}

		//Data we need for each free
		Class_Size_TAD *class_size_tad;
		allocation_metadata allocData = init_allocation(0, FREE);
		allocData.address = reinterpret_cast <uint64_t> (ptr);

		//Do before free
		doBefore(&allocData);

		//Do free
		RealX::free(ptr);

		//Do after free
		doAfter(&allocData);

		//Update free counters
		allocData.classSize = updateFreeCounters(allocData.address);

		//Update atomics
		cycles_free.fetch_add ((allocData.tsc_after - allocData.tsc_before), relaxed);
		num_free.fetch_add(1, relaxed);

		class_size_tad = nullptr;
		if(threadToCSM.find(allocData.tid, &class_size_tad)){
			if(d_pmuData){
				fprintf(stderr, "current class size: %lu\n", allocData.classSize);
				fprintf(stderr, "found tid -> class_size_tad\n");
			}

			if(class_size_tad->find(allocData.classSize, &(allocData.tad))){
				if(d_pmuData)
					fprintf(stderr, "found class_size_tad -> tad\n");

				allocData.tad->numFrees += 1;
				allocData.tad->numFreeFaults += allocData.after.faults - allocData.before.faults;
				allocData.tad->numFreeTlbMisses += allocData.after.tlb_read_misses - allocData.before.tlb_read_misses;
				// tad->numFreeTlbMisses += after.tlb_write_misses - before.tlb_write_misses;
				allocData.tad->numFreeCacheMisses += allocData.after.cache_misses - allocData.before.cache_misses;
				allocData.tad->numFreeCacheRefs += allocData.after.cache_refs - allocData.before.cache_refs;
				allocData.tad->numFreeInstrs += allocData.after.instructions - allocData.before.instructions;
			}
		}

		//printf("Class_Size_TAD associated with threadToCSM %p = %p\n", &threadToCSM, class_size_tad);
		if (((allocData.after.instructions - allocData.before.instructions) != 0) && d_pmuData){
			fprintf(stderr, "Free from thread %d\n"
							"Num faults:              %ld\n"
							"Num TLB read misses:     %ld\n"
							"Num TLB write misses:    %ld\n"
							"Num cache misses:        %ld\n"
							"num cache refs:          %ld\n"
							"Num instructions:        %ld\n\n",
			allocData.tid, allocData.after.faults - allocData.before.faults,
			allocData.after.tlb_read_misses - allocData.before.tlb_read_misses,
			allocData.after.tlb_write_misses - allocData.before.tlb_write_misses,
			allocData.after.cache_misses - allocData.before.cache_misses,
			allocData.after.cache_refs - allocData.before.cache_refs,
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

		num_realloc.fetch_add(1, relaxed);

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

		//Do after
		// doAfter(&after, &tsc_after);
		doAfter(&allocData);

        // cyclesForRealloc = tsc_after - tsc_before;
        allocData.cycles = allocData.tsc_after - allocData.tsc_before;
		allocData.address = reinterpret_cast <uint64_t> (object);

		//Gets overhead, address usage, mmap usage, memHWM
		// analyzeAllocation(sz, address, cyclesForRealloc, classSize, &reused);
		analyzeAllocation(&allocData);

		//Get perf info
		//analyzePerfInfo(&before, &after, classSize, &reused, tid);

		totalMemAllocated.fetch_add(sz, relaxed);

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

	addrinfo addr2line(void * addr) {
		static bool initialized = false;
		static int fd[2][2];
		char strCallsite[20];
		char strInfo[512];
		addrinfo info;

		if(!initialized) {
			if((pipe(fd[0]) == -1) || (pipe(fd[1]) == -1)) {
				perror("error: unable to create pipe for addr2line\n");
				fprintf(thrData.output,
						"error: unable to create pipe for addr2line\n");
				strcpy(info.exename, "error");
				info.lineNum = 0;
				return info;
			}

			pid_t parent;
			switch(parent = fork()) {
				case -1:
					perror("error: unable to fork addr2line process\n");
					fprintf(thrData.output,
							"error: unable to fork addr2line process\n");
					strcpy(info.exename, "error");
					info.lineNum = 0;
					return info;
				case 0:		// child
					dup2(fd[1][0], STDIN_FILENO);
					dup2(fd[0][1], STDOUT_FILENO);
					// Close unneeded pipe ends for the child
					close(fd[0][0]);
					close(fd[1][1]);
					execlp("addr2line", "addr2line", "-s", "-e",
							program_invocation_name, "--", (char *)NULL);
					exit(EXIT_FAILURE);	// if we're still here then exec failed
					break;
				default:	// parent
					// Close unneeded pipe ends for the parent
					close(fd[0][1]);
					close(fd[1][0]);
					initialized = true;
			}
		}

		sprintf(strCallsite, "%p\n", addr);
		int szToWrite = strlen(strCallsite);
		if(write(fd[1][1], strCallsite, szToWrite) < szToWrite) {
			perror("error: incomplete write to pipe facing addr2line\n");
			fprintf(thrData.output,
					"error: incomplete write to pipe facing addr2line\n");
		}

		if(read(fd[0][0], strInfo, 512) == -1) {
			perror("error: unable to read from pipe facing addr2line\n");
			fprintf(thrData.output,
					"error: unable to read from pipe facing addr2line\n");
			strcpy(info.exename, "error");
			info.lineNum = 0;
			return info;
		}

		// Tokenize the return string, breaking apart by ':'.
		// Take the second token, which will be the line number.
		// Only copies the first 14 characters of the file name in order to
		// prevent misalignment in the program output.
		char * token = strtok(strInfo, ":");
		strncpy(info.exename, token, 14);
		info.exename[14] = '\0';	// null terminate the exename field
		token = strtok(NULL, ":");
		info.lineNum = atoi(token);

		return info;
	}

	// PTHREAD_CREATE
	int pthread_create(pthread_t * tid, const pthread_attr_t * attr,
							 void *(*start_routine)(void *), void * arg) {
		if (!realInitialized) RealX::initializer();

		int result = xthreadx::thread_create(tid, attr, start_routine, arg);
		threads.fetch_add(1, relaxed);
		return result;
	}

	// PTHREAD_JOIN
	int pthread_join(pthread_t thread, void **retval) {
		if (!realInitialized) RealX::initializer();

		int result = RealX::pthread_join (thread, retval);
        //fprintf(stderr, "Thread: %lX\n", thread);
		return result;
	}

	// PTHREAD_MUTEX_LOCK
	int pthread_mutex_lock(pthread_mutex_t *mutex) {
		if (!realInitialized) RealX::initializer();

		uint64_t tid = pthread_self();
		uint64_t lockAddr = reinterpret_cast <uint64_t> (mutex);

		ThreadContention* tc;

		//Is this thread doing allocation
		if (inAllocation) {

			//Have we encountered this lock before?
			LC* thisLock;
			if (lockUsage.find (lockAddr, &thisLock)) {
				thisLock->contention++;

				if (thisLock->contention > thisLock->maxContention)
					thisLock->maxContention = thisLock->contention;

				if (thisLock->contention > 1) {
					waiting = true;
					timeAttempted = rdtscp();
				}
			}

			//Add lock to lockUsage hashmap
			else {
				num_pthread_mutex_locks.fetch_add(1, relaxed);
				lockUsage.insertIfAbsent(lockAddr, newLC());
			}
		}

		//Aquire the lock
		int result = RealX::pthread_mutex_lock (mutex);
		if (waiting) {
			timeWaiting += ((rdtscp()) - timeAttempted);
			waiting = false;
			if (threadContention.find(tid, &tc)) {
				tc->mutex_waits++;
				tc->mutex_wait_cycles += timeWaiting;
			}
			else {
				threadContention.insertIfAbsent(tid, newThreadContention(tid));
				if (threadContention.find(tid, &tc)) {
					tc->mutex_waits++;
					tc->mutex_wait_cycles += timeWaiting;
				}
				else {
					fprintf(stderr, "failed to insert tid into threadContention, mutex\n");
					abort();
				}
			}
		}
		return result;
	}

	// PTHREAD_MUTEX_TRYLOCK
	int pthread_mutex_trylock (pthread_mutex_t *mutex) {
		if (!realInitialized) RealX::initializer();

		if (!mapsInitialized)
			return RealX::pthread_mutex_trylock (mutex);

		uint64_t lockAddr = reinterpret_cast <uint64_t> (mutex);
		uint64_t tid = pthread_self();
		ThreadContention* tc;

		//Is this thread doing allocation
		if (inAllocation) {

			//Have we encountered this lock before?
			LC* thisLock;
			if (lockUsage.find (lockAddr, &thisLock)) {
				thisLock->contention++;
				if (thisLock->contention > thisLock->maxContention)
					thisLock->maxContention = thisLock->contention;
			}

			//Add lock to lockUsage hashmap
			else {
				num_pthread_mutex_locks.fetch_add(1, relaxed);
				LC* lc = newLC();
				lockUsage.insertIfAbsent (lockAddr, lc);
			}
		}

		//Try to aquire the lock
		int result = RealX::pthread_mutex_trylock (mutex);
		if (result != 0) {
			num_trylock.fetch_add(1, relaxed);
			if (threadContention.find(tid, &tc)) {
				tc->mutex_trylock_fails++;
			}
			else {
				threadContention.insertIfAbsent(tid, newThreadContention(tid));
				if (threadContention.find(tid, &tc)) {
					tc->mutex_trylock_fails++;
				}
				else {
					fprintf(stderr, "failed to insert tid into threadContention, mutex_trylock\n");
					abort();
				}
			}
		}
		return result;
	}

	// PTHREAD_MUTEX_UNLOCK
	int pthread_mutex_unlock(pthread_mutex_t *mutex) {
		if (!realInitialized) RealX::initializer();

		if (!mapsInitialized)
			return RealX::pthread_mutex_unlock (mutex);

		if (inAllocation) {
				uint64_t lockAddr = reinterpret_cast <uint64_t> (mutex);

				//Decrement contention on this LC if this lock is
				//in our map
				LC* thisLock;
				if (lockUsage.find (lockAddr, &thisLock)) {
						thisLock->contention--;
				}
		}

		return RealX::pthread_mutex_unlock (mutex);
	}

	// MADVISE
	int madvise(void *addr, size_t length, int advice){
		if (!realInitialized) RealX::initializer();

		if (advice == MADV_DONTNEED)
			num_dontneed.fetch_add(1, relaxed);

		uint64_t timeStart = 0;
		uint64_t timeStop = 0;
		bool madvise_wait = false;
		ThreadContention* tc;
		uint64_t tid = pthread_self();

		if (madvise_active.load()) {
			madvise_wait = true;
			timeStart = rdtscp();
		}

		madvise_active = true;
		int result = RealX::madvise(addr, length, advice);
		madvise_active = false;

		if (madvise_wait) {
			timeStop = rdtscp();

			if (threadContention.find(tid, &tc)) {
				tc->madvise_waits++;
				tc->madvise_wait_cycles += (timeStop - timeStart);
			}
			else {
				threadContention.insertIfAbsent(tid, newThreadContention(tid));
				if (threadContention.find(tid, &tc)) {
					tc->madvise_waits++;
					tc->madvise_wait_cycles += (timeStop - timeStart);
				}
				else {
					fprintf(stderr, "failed to insert tid into threadContention, mmap\n");
					abort();
				}
			}
		}
		return result;
	}

	// SBRK
    void *sbrk(intptr_t increment){
		if (!realInitialized) RealX::initializer();
        if(profilerInitialized != INITIALIZED) return RealX::sbrk(increment);

		uint64_t timeStart = 0;
		uint64_t timeStop = 0;
		bool sbrk_wait = false;
		ThreadContention* tc;
		uint64_t tid = pthread_self();

		if (sbrk_active.load()) {
			sbrk_wait = true;
			timeStart = rdtscp();
		}

		sbrk_active = true;
        void *retptr = RealX::sbrk(increment);
		sbrk_active = false;

		if (sbrk_wait) {
			timeStop = rdtscp();

			if (threadContention.find(tid, &tc)) {
				tc->sbrk_waits++;
				tc->sbrk_wait_cycles += (timeStop - timeStart);
			}
			else {
				threadContention.insertIfAbsent(tid, newThreadContention(tid));
				if (threadContention.find(tid, &tc)) {
					tc->sbrk_waits++;
					tc->sbrk_wait_cycles += (timeStop - timeStart);
				}
				else {
					fprintf(stderr, "failed to insert tid into threadContention, mmap\n");
					abort();
				}
			}
		}

        uint64_t newProgramBreak = (uint64_t) RealX::sbrk(0);
        uint64_t oldProgramBreak = (uint64_t) retptr;
        uint64_t sizeChange = newProgramBreak - oldProgramBreak;

        size_sbrk.fetch_add(sizeChange, relaxed);
        num_sbrk.fetch_add(1, relaxed);

        return retptr;
    }

	// MPROTECT
	int mprotect (void* addr, size_t len, int prot) {
		if (!realInitialized) RealX::initializer();

		num_mprotect.fetch_add(1, relaxed);
		if (d_mprotect)
			printf ("mprotect/found= %s, addr= %p, len= %zu, prot= %d\n",
		mappingEditor(addr, len, prot) ? "true" : "false", addr, len, prot);

		return RealX::mprotect (addr, len, prot);
	}

	// MMAP
	void * yymmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
		if (!realInitialized) RealX::initializer();

		if (!mapsInitialized) return RealX::mmap (addr, length, prot, flags, fd, offset);

		if (inMmap) return RealX::mmap (addr, length, prot, flags, fd, offset);

		uint64_t timeStart = 0;
		uint64_t timeStop = 0;
		bool mmap_wait = false;

		if (mmap_active.load()) {
			timeStart = rdtscp();
			mmap_wait = true;
		}

		ThreadContention* tc;
		uint64_t tid = pthread_self();

		//thread_local
		inMmap = true;

		//global atomic
		mmap_active = true;
		void* retval = RealX::mmap(addr, length, prot, flags, fd, offset);
		mmap_active = false;

		if (mmap_wait) {
			timeStop = rdtscp();
			if (threadContention.find(tid, &tc)) {
				tc->mmap_waits++;
				tc->mmap_wait_cycles += (timeStop - timeStart);
			}
			else {
				threadContention.insertIfAbsent(tid, newThreadContention(tid));
				if (threadContention.find(tid, &tc)) {
					tc->mmap_waits++;
					tc->mmap_wait_cycles += (timeStop - timeStart);
				}
				else {
					fprintf(stderr, "failed to insert tid into threadContention, mmap\n");
					abort();
				}
			}
		}

		//uint64_t address = reinterpret_cast <uint64_t> (retval);

		//If this thread currently doing an allocation
		if (inAllocation) {
			if (d_mmap) printf ("mmap direct from allocation function: length= %zu, prot= %d\n", length, prot);
			malloc_mmaps.fetch_add (1, relaxed);
			//mappings.insert(address, newMmapTuple(address, length, prot, 'a'));
		}

		//Need to check if selfmap.getInstance().getTextRegions() has
		//ran. If it hasn't, we can't call isAllocatorInCallStack()
		else if (selfmapInitialized && isAllocatorInCallStack()) {
			if (d_mmap) printf ("mmap allocator in callstack: length= %zu, prot= %d\n", length, prot);
			//mappings.insert(address, newMmapTuple(address, length, prot, 's'));
		}
		else {
			if (d_mmap) printf ("mmap from unknown source: length= %zu, prot= %d\n", length, prot);
			//mappings.insert(address, newMmapTuple(address, length, prot, 'u'));
		}

		total_mmaps++;

		inMmap = false;
		return retval;
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
	getAddressUsage(metadata->size, metadata->address, metadata->cycles);

	//Update mmap region info
	getMappingsUsage(metadata->size, metadata->address, metadata->classSize);

	// Analyze perfinfo
	analyzePerfInfo(metadata);
}

size_t updateFreeCounters(uint64_t address) {
	
	ObjectTuple* t;
	size_t size = 0;
    size_t current_class_size = 0;

	if (addressUsage.find(address, &t)){
		size = t->szUsed;

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

void getAddressUsage(size_t size, uint64_t address, uint64_t cycles) {

	ObjectTuple* t;
	//Has this address been used before
	if (addressUsage.find (address, &t)) {
		t->szUsed = size;
		reused_address.fetch_add (1, relaxed);
		cycles_reused.fetch_add (cycles, relaxed);
	}

	else {
		new_address.fetch_add (1, relaxed);
		cycles_new.fetch_add (cycles, relaxed);
		addressUsage.insertIfAbsent (address, newObjectTuple(address, size));
	}
}

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
		if (bibop) getAlignment(size, classSize);

		//Check for memory blowup
		getBlowup(size, classSize, reused);

		//Add metadata for this overhead object
		getMetadata(classSize);
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
		o->addAlignment(alignmentBytes);
	}
}

void getBlowup (size_t size, size_t classSize, bool* reused) {

	short class_size_index = getClassSizeIndex(size);
	
	//If I have free objects on my local list
	if (localFreeArray[class_size_index] > 0) {
		*reused = true;
		localFreeArray[class_size_index]--;
		globalFreeArray[class_size_index]--;
		if (d_getBlowup) printf ("I have free. globalFreeArray[%d]--\n", class_size_index);
		return;
	}

	//I don't have free objects on my local list, check global
	else {
		if (globalFreeArray[class_size_index] > 0) {
			globalFreeArray[class_size_index]--;
			if (d_getBlowup) printf ("I don't have free. globalFreeArray[%d]--\n", class_size_index);
		}
		//I don't have free objects, and no one else does either
		else {
			return;
		}
	}

	//If we've made it here, this is a blowup allocation
	//Increment the blowup for this class size
	Overhead* o;
	if (overhead.find(classSize, &o)) {
		o->addBlowup(size);
	}
	blowup_allocations.fetch_add(1, relaxed);
}

//Return the appropriate class size that this object should be in
size_t getClassSizeFor (size_t size) {

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

thread_alloc_data* newTad(){
    thread_alloc_data* tad = (thread_alloc_data*) myMalloc(sizeof(thread_alloc_data));

    tad->numMallocFaults = 0;
    tad->numReallocFaults = 0;
    tad->numFreeFaults = 0;

    tad->numMallocTlbReadMisses = 0;
    tad->numMallocTlbWriteMisses = 0;

    tad->numReallocTlbReadMisses = 0;
    tad->numReallocTlbWriteMisses = 0;

    tad->numFreeTlbMisses = 0;

    tad->numMallocCacheMisses = 0;
    tad->numReallocCacheMisses = 0;
    tad->numFreeCacheMisses = 0;

    tad->numMallocCacheRefs = 0;
    tad->numReallocCacheRefs = 0;
    tad->numFreeCacheRefs = 0;

    tad->numMallocInstrs = 0;
    tad->numReallocInstrs = 0;
    tad->numFreeInstrs = 0;

    tad->numMallocFaultsFFL = 0;
    tad->numReallocFaultsFFL = 0;

    tad->numMallocTlbReadMissesFFL = 0;
    tad->numMallocTlbWriteMissesFFL = 0;

    tad->numReallocTlbReadMissesFFL = 0;
    tad->numReallocTlbWriteMissesFFL = 0;

    tad->numMallocCacheMissesFFL = 0;
    tad->numReallocCacheMissesFFL = 0;

    tad->numMallocCacheRefsFFL = 0;
    tad->numReallocCacheRefsFFL = 0;

    tad->numMallocInstrsFFL = 0;
    tad->numReallocInstrsFFL = 0;

    return tad;
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

	#ifdef USE_THREAD_LOCAL
	if (myLocalMemInitialized) {
		return myLocalMalloc(size);
	}
	#endif

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

	#ifdef USE_THREAD_LOCAL
	if (ptr >= myLocalMem && ptr <= myLocalMemEnd) {
		myLocalFree(ptr);
		return;
	}
	#endif

	myMemLock.lock();
	if (ptr >= (void*)myMem && ptr <= myMemEnd) {
		myMemAllocations--;
		if(myMemAllocations == 0) myMemPosition = 0;
	}
	myMemLock.unlock();
}

//Holy
void globalizeThreadAllocData(){
    //HashMap<uint64_t, thread_alloc_data*, spinlock>::iterator i;
	if (d_trace) fprintf(stderr, "Entering globalizeThreadAllocData()\n");
    for(auto it1 : threadToCSM){
        for(auto it2 : *it1.getData()){

            allThreadsTadMap[it2.getKey()]->numMallocs += it2.getData()->numMallocs;
            allThreadsTadMap[it2.getKey()]->numReallocs += it2.getData()->numReallocs;
            allThreadsTadMap[it2.getKey()]->numCallocs += it2.getData()->numCallocs;
            allThreadsTadMap[it2.getKey()]->numFrees += it2.getData()->numFrees;

            allThreadsTadMap[it2.getKey()]->numMallocsFFL += it2.getData()->numMallocsFFL;
            allThreadsTadMap[it2.getKey()]->numReallocsFFL += it2.getData()->numReallocsFFL;
            allThreadsTadMap[it2.getKey()]->numCallocsFFL += it2.getData()->numCallocsFFL;

            allThreadsTadMap[it2.getKey()]->numMallocFaults += it2.getData()->numMallocFaults;
            allThreadsTadMap[it2.getKey()]->numReallocFaults += it2.getData()->numReallocFaults;
            allThreadsTadMap[it2.getKey()]->numCallocFaults += it2.getData()->numCallocFaults;
            allThreadsTadMap[it2.getKey()]->numFreeFaults += it2.getData()->numFreeFaults;

            allThreadsTadMap[it2.getKey()]->numMallocTlbReadMisses += it2.getData()->numMallocTlbReadMisses;
            allThreadsTadMap[it2.getKey()]->numMallocTlbWriteMisses += it2.getData()->numMallocTlbWriteMisses;
            allThreadsTadMap[it2.getKey()]->numReallocTlbReadMisses += it2.getData()->numReallocTlbReadMisses;
            allThreadsTadMap[it2.getKey()]->numReallocTlbWriteMisses += it2.getData()->numReallocTlbWriteMisses;
            allThreadsTadMap[it2.getKey()]->numCallocTlbReadMisses += it2.getData()->numCallocTlbReadMisses;
            allThreadsTadMap[it2.getKey()]->numCallocTlbWriteMisses += it2.getData()->numCallocTlbWriteMisses;
            allThreadsTadMap[it2.getKey()]->numFreeTlbMisses += it2.getData()->numFreeTlbMisses;

            allThreadsTadMap[it2.getKey()]->numMallocCacheMisses += it2.getData()->numMallocCacheMisses;
            allThreadsTadMap[it2.getKey()]->numReallocCacheMisses += it2.getData()->numReallocCacheMisses;
            allThreadsTadMap[it2.getKey()]->numCallocCacheMisses += it2.getData()->numCallocCacheMisses;
            allThreadsTadMap[it2.getKey()]->numFreeCacheMisses += it2.getData()->numFreeCacheMisses;

            allThreadsTadMap[it2.getKey()]->numMallocCacheRefs += it2.getData()->numMallocCacheRefs;
            allThreadsTadMap[it2.getKey()]->numReallocCacheRefs += it2.getData()->numReallocCacheRefs;
            allThreadsTadMap[it2.getKey()]->numCallocCacheRefs += it2.getData()->numCallocCacheRefs;
            allThreadsTadMap[it2.getKey()]->numFreeCacheRefs += it2.getData()->numFreeCacheRefs;

            allThreadsTadMap[it2.getKey()]->numMallocInstrs += it2.getData()->numMallocInstrs;
            allThreadsTadMap[it2.getKey()]->numReallocInstrs += it2.getData()->numReallocInstrs;
            allThreadsTadMap[it2.getKey()]->numCallocInstrs += it2.getData()->numCallocInstrs;
            allThreadsTadMap[it2.getKey()]->numFreeInstrs += it2.getData()->numFreeInstrs;

            allThreadsTadMap[it2.getKey()]->numMallocFaultsFFL += it2.getData()->numMallocFaultsFFL;
            allThreadsTadMap[it2.getKey()]->numReallocFaultsFFL += it2.getData()->numReallocFaultsFFL;
            allThreadsTadMap[it2.getKey()]->numCallocFaultsFFL += it2.getData()->numCallocFaultsFFL;

            allThreadsTadMap[it2.getKey()]->numMallocTlbReadMissesFFL += it2.getData()->numMallocTlbReadMissesFFL;
            allThreadsTadMap[it2.getKey()]->numMallocTlbWriteMissesFFL += it2.getData()->numMallocTlbWriteMissesFFL;

            allThreadsTadMap[it2.getKey()]->numReallocTlbReadMissesFFL += it2.getData()->numReallocTlbReadMissesFFL;
            allThreadsTadMap[it2.getKey()]->numReallocTlbWriteMissesFFL += it2.getData()->numReallocTlbWriteMissesFFL;

			/* NOTE(STEFEN): CALLOC TLB ARE STILL MISSING RETARD */
            allThreadsTadMap[it2.getKey()]->numCallocTlbReadMissesFFL += it2.getData()->numCallocTlbReadMissesFFL;
            allThreadsTadMap[it2.getKey()]->numCallocTlbWriteMissesFFL += it2.getData()->numCallocTlbWriteMissesFFL;

            allThreadsTadMap[it2.getKey()]->numMallocCacheMissesFFL += it2.getData()->numMallocCacheMissesFFL;
            allThreadsTadMap[it2.getKey()]->numReallocCacheMissesFFL += it2.getData()->numReallocCacheMissesFFL;
            allThreadsTadMap[it2.getKey()]->numCallocCacheMissesFFL += it2.getData()->numCallocCacheMissesFFL;

            allThreadsTadMap[it2.getKey()]->numMallocCacheRefsFFL += it2.getData()->numMallocCacheRefsFFL;
            allThreadsTadMap[it2.getKey()]->numReallocCacheRefsFFL += it2.getData()->numReallocCacheRefsFFL;
            allThreadsTadMap[it2.getKey()]->numCallocCacheRefsFFL += it2.getData()->numCallocCacheRefsFFL;

            allThreadsTadMap[it2.getKey()]->numMallocInstrsFFL += it2.getData()->numMallocInstrsFFL;
            allThreadsTadMap[it2.getKey()]->numReallocInstrsFFL += it2.getData()->numReallocInstrsFFL;
            allThreadsTadMap[it2.getKey()]->numCallocInstrsFFL += it2.getData()->numCallocInstrsFFL;
        /*
            allThreadsNumMallocFaults.fetch_add(it2.getData()->numMallocFaults);
            allThreadsNumReallocFaults.fetch_add(it2.getData()->numReallocFaults);
            allThreadsNumFreeFaults.fetch_add(it2.getData()->numFreeFaults);

            allThreadsNumMallocTlbMisses.fetch_add(it2.getData()->numMallocTlbReadMisses);
            allThreadsNumReallocTlbMisses.fetch_add(it2.getData()->numReallocTlbMisses);
            allThreadsNumFreeTlbMisses.fetch_add(it2.getData()->numFreeTlbMisses);

            allThreadsNumMallocCacheMisses.fetch_add(it2.getData()->numMallocCacheMisses);
            allThreadsNumReallocCacheMisses.fetch_add(it2.getData()->numReallocCacheMisses);
            allThreadsNumFreeCacheMisses.fetch_add(it2.getData()->numFreeCacheMisses);

            allThreadsNumMallocCacheRefs.fetch_add(it2.getData()->numMallocCacheRefs);
            allThreadsNumReallocCacheRefs.fetch_add(it2.getData()->numReallocCacheRefs);
            allThreadsNumFreeCacheRefs.fetch_add(it2.getData()->numFreeCacheRefs);

            allThreadsNumMallocInstrs.fetch_add(it2.getData()->numMallocInstrs);
            allThreadsNumReallocInstrs.fetch_add(it2.getData()->numReallocInstrs);
            allThreadsNumFreeInstrs.fetch_add(it2.getData()->numFreeInstrs);

            allThreadsNumMallocFaultsFFL.fetch_add(it2.getData()->numMallocFaultsFFL);
            allThreadsNumReallocFaultsFFL.fetch_add(it2.getData()->numReallocFaultsFFL);

            allThreadsNumMallocTlbMissesFFL.fetch_add(it2.getData()->numMallocTlbMissesFFL);
            allThreadsNumReallocTlbMissesFFL.fetch_add(it2.getData()->numReallocTlbMissesFFL);

            allThreadsNumMallocCacheMissesFFL.fetch_add(it2.getData()->numMallocCacheMissesFFL);
            allThreadsNumReallocCacheMissesFFL.fetch_add(it2.getData()->numReallocCacheMissesFFL);

            allThreadsNumMallocCacheRefsFFL.fetch_add(it2.getData()->numMallocCacheRefsFFL);
            allThreadsNumReallocCacheRefsFFL.fetch_add(it2.getData()->numReallocCacheRefsFFL);

            allThreadsNumMallocInstrsFFL.fetch_add(it2.getData()->numMallocInstrsFFL);
            allThreadsNumReallocInstrsFFL.fetch_add(it2.getData()->numReallocInstrsFFL);
        */
        }
    }
  /*

//        fprintf (thrData.output, "\n>>> Thread ID:     %lu\n", it.getKey());
//        fprintf (thrData.output, ">>> page faults    %ld\n", it.getData()->numFaults);
//        fprintf (thrData.output, ">>> TLB misses     %ld\n", it.getData()->numTlbMisses);
//        fprintf (thrData.output, ">>> cache misses   %ld\n", it.getData()->numCacheMisses);
//        fprintf (thrData.output, ">>> cache refs     %ld\n", it.getData()->numCacheRefs);
//        fprintf (thrData.output, ">>> num instr      %ld\n",it.getData()->numInstrs);

    } */
}

void writeAllocData () {
	if (d_trace) fprintf(stderr, "Entering writeAllocData()\n");
	unsigned print_new_address = new_address.load(relaxed);
	unsigned print_reused_address = reused_address.load(relaxed);
	unsigned print_num_free = num_free.load(relaxed);

	fprintf(thrData.output, "\n");
	fprintf(thrData.output, ">>> num_malloc               %u\n", num_malloc.load(relaxed));
	fprintf(thrData.output, ">>> num_actual_malloc        %u\n", num_actual_malloc.load(relaxed));
	fprintf(thrData.output, ">>> num_calloc               %u\n", num_calloc.load(relaxed));
	fprintf(thrData.output, ">>> num_realloc              %u\n", num_realloc.load(relaxed));
	fprintf(thrData.output, ">>> new_address              %u\n", print_new_address);
	fprintf(thrData.output, ">>> reused_address           %u\n", print_reused_address);
	fprintf(thrData.output, ">>> num_free                 %u\n", print_num_free);
	fprintf(thrData.output, ">>> malloc_mmaps             %u\n", malloc_mmaps.load(relaxed));
	fprintf(thrData.output, ">>> total_mmaps              %u\n", total_mmaps.load(relaxed));
	fprintf(thrData.output, ">>> malloc_mmap_threshold    %zu\n", malloc_mmap_threshold);
	fprintf(thrData.output, ">>> threads                  %u\n", threads.load(relaxed));

	if (print_new_address)
	fprintf(thrData.output, ">>> cycles_new               %lu\n",
							(cycles_new.load(relaxed) / print_new_address));
	else
	fprintf(thrData.output, ">>> cycles_new               N/A\n");

	if (print_reused_address)
	fprintf(thrData.output, ">>> cycles_reused            %lu\n",
							(cycles_reused.load(relaxed) / print_reused_address));
	else
	fprintf(thrData.output, ">>> cycles_reused            N/A\n");

	if (print_num_free)
	fprintf(thrData.output, ">>> cycles_free              %lu\n",
							(cycles_free.load(relaxed) / print_num_free));
	else
	fprintf(thrData.output, ">>> cycles_free              N/A\n");

	fprintf(thrData.output, ">>> pthread_mutex_lock       %u\n", num_pthread_mutex_locks.load(relaxed));
	fprintf(thrData.output, ">>> num_trylock              %u\n", num_trylock.load(relaxed));
	fprintf(thrData.output, ">>> num_madvise              %u\n", num_madvise.load(relaxed));
    fprintf(thrData.output, ">>> num_dontneed             %u\n", num_dontneed.load(relaxed));
	fprintf(thrData.output, ">>> num_sbrk                 %u\n", num_sbrk.load(relaxed));
	fprintf(thrData.output, ">>> size_sbrk                %u\n", size_sbrk.load(relaxed));
	fprintf(thrData.output, ">>> num_mprotect             %u\n", num_mprotect.load(relaxed));
	fprintf(thrData.output, ">>> blowup_allocations       %u\n", blowup_allocations.load(relaxed));
	fprintf(thrData.output, ">>> blowup_bytes             %zu\n", blowup_bytes);
	fprintf(thrData.output, ">>> alignment                %zu\n", alignment);
	fprintf(thrData.output, ">>> metadata_object          %zu\n", metadata_object);
	fprintf(thrData.output, ">>> metadata_overhead        %zu\n", metadata_overhead);
	fprintf(thrData.output, ">>> totalSizeAlloc           %zu\n", totalSizeAlloc);
	fprintf(thrData.output, ">>> totalMemOverhead         %zu\n", totalMemOverhead);
	fprintf(thrData.output, ">>> memEfficiency            %.2f%%\n", memEfficiency);

	writeOverhead();
	if (d_write_mappings) writeMappings();
	if (d_write_tad) writeThreadMaps();
	writeThreadContention();

	fflush (thrData.output);
}

void writeThreadContention() {

	fprintf (thrData.output, "\n--------------------ThreadContention--------------------\n\n");

	for (auto tc : threadContention) {
		auto data = tc.getData();

		fprintf (thrData.output, ">>> tid                  %lu\n", data->tid);
		fprintf (thrData.output, ">>> mutex_waits          %lu\n", data->mutex_waits);
		fprintf (thrData.output, ">>> mutex_wait_cycles    %lu\n", data->mutex_wait_cycles);
		fprintf (thrData.output, ">>> mutex_trylock_fails  %lu\n", data->mutex_trylock_fails);
		fprintf (thrData.output, ">>> mmap_waits           %lu\n", data->mmap_waits);
		fprintf (thrData.output, ">>> mmap_wait_cycles     %lu\n", data->mmap_wait_cycles);
		fprintf (thrData.output, ">>> sbrk_waits           %lu\n", data->sbrk_waits);
		fprintf (thrData.output, ">>> sbrk_wait_cycles     %lu\n", data->sbrk_wait_cycles);
		fprintf (thrData.output, ">>> madvise_waits        %lu\n", data->madvise_waits);
		fprintf (thrData.output, ">>> madvise_wait_cycles  %lu\n\n", data->madvise_wait_cycles);
	}
}

void writeThreadMaps () {

    fprintf (thrData.output, "\n>>>>> TOTALS NOT FROM FREELIST <<<<<\n");

    for (auto const &p : allThreadsTadMap){
        fprintf (thrData.output, "Class Size:       %lu\n", p.first);

        fprintf (thrData.output, "mallocs                     %lu\n", p.second->numMallocs);
        fprintf (thrData.output, "reallocs                    %lu\n", p.second->numReallocs);
        fprintf (thrData.output, "callocs                     %lu\n", p.second->numCallocs);
        fprintf (thrData.output, "frees                       %lu\n", p.second->numFrees);

        fprintf (thrData.output, "malloc faults               %lu\n", p.second->numMallocFaults);
        fprintf (thrData.output, "realloc faults              %lu\n", p.second->numReallocFaults);
        fprintf (thrData.output, "calloc faults               %lu\n", p.second->numCallocFaults);
        fprintf (thrData.output, "free faults                 %lu\n", p.second->numFreeFaults);

        fprintf (thrData.output, "malloc tlb read misses      %lu\n", p.second->numMallocTlbReadMisses);
        fprintf (thrData.output, "malloc tlb write misses     %lu\n", p.second->numMallocTlbWriteMisses);
        fprintf (thrData.output, "realloc tlb read misses     %lu\n", p.second->numReallocTlbReadMisses);
        fprintf (thrData.output, "realloc tlb write misses    %lu\n", p.second->numReallocTlbWriteMisses);
        fprintf (thrData.output, "calloc tlb read misses      %lu\n", p.second->numCallocTlbReadMisses);
        fprintf (thrData.output, "calloc tlb write misses     %lu\n", p.second->numCallocTlbWriteMisses);
        fprintf (thrData.output, "free tlb misses             %lu\n", p.second->numFreeTlbMisses);

        fprintf (thrData.output, "malloc cache misses         %lu\n", p.second->numMallocCacheMisses);
        fprintf (thrData.output, "realloc cache misses        %lu\n", p.second->numReallocCacheMisses);
        fprintf (thrData.output, "calloc cache misses         %lu\n", p.second->numCallocCacheMisses);
        fprintf (thrData.output, "free cache misses           %lu\n", p.second->numFreeCacheMisses);

        fprintf (thrData.output, "malloc cache refs           %lu\n", p.second->numMallocCacheRefs);
        fprintf (thrData.output, "realloc cache refs          %lu\n", p.second->numReallocCacheRefs);
        fprintf (thrData.output, "calloc cache refs           %lu\n", p.second->numCallocCacheRefs);
        fprintf (thrData.output, "free cache refs             %lu\n", p.second->numFreeCacheRefs);

        fprintf (thrData.output, "num malloc instr            %lu\n", p.second->numMallocInstrs);
        fprintf (thrData.output, "num realloc instr           %lu\n", p.second->numReallocInstrs);
        fprintf (thrData.output, "num calloc instr            %lu\n", p.second->numCallocInstrs);
        fprintf (thrData.output, "num free instr              %lu\n", p.second->numFreeInstrs);
        fprintf (thrData.output, "\n");
    }

    fprintf (thrData.output, "\n>>>>> TOTALS FROM FREELIST <<<<<\n");

    for (auto const &p : allThreadsTadMap){
        fprintf (thrData.output, "Class Size:    %lu\n",   p.first);

        fprintf (thrData.output, "mallocs                     %lu\n",   p.second->numMallocsFFL);
        fprintf (thrData.output, "reallocs                    %lu\n",   p.second->numReallocsFFL);
        fprintf (thrData.output, "callocs                     %lu\n",   p.second->numCallocsFFL);

        fprintf (thrData.output, "malloc faults               %lu\n",   p.second->numMallocFaultsFFL);
        fprintf (thrData.output, "realloc faults              %lu\n",   p.second->numReallocFaultsFFL);
        fprintf (thrData.output, "calloc faults               %lu\n",   p.second->numCallocFaultsFFL);

        fprintf (thrData.output, "malloc tlb read misses      %lu\n",   p.second->numMallocTlbReadMissesFFL);
        fprintf (thrData.output, "malloc tlb write misses     %lu\n",   p.second->numMallocTlbWriteMissesFFL);

        fprintf (thrData.output, "realloc tlb read misses     %lu\n",   p.second->numReallocTlbReadMissesFFL);
        fprintf (thrData.output, "realloc tlb write misses    %lu\n",   p.second->numReallocTlbWriteMissesFFL);

        fprintf (thrData.output, "calloc tlb read misses      %lu\n",   p.second->numCallocTlbReadMissesFFL);
        fprintf (thrData.output, "calloc tlb write misses     %lu\n",   p.second->numCallocTlbWriteMissesFFL);

        fprintf (thrData.output, "malloc cache misses         %lu\n",   p.second->numMallocCacheMissesFFL);
        fprintf (thrData.output, "realloc cache misses        %lu\n",   p.second->numReallocCacheMissesFFL);
        fprintf (thrData.output, "calloc cache misses         %lu\n",   p.second->numCallocCacheMissesFFL);

        fprintf (thrData.output, "malloc cache refs           %lu\n",   p.second->numMallocCacheRefsFFL);
        fprintf (thrData.output, "realloc cache refs          %lu\n",   p.second->numReallocCacheRefsFFL);
        fprintf (thrData.output, "calloc cache refs           %lu\n",   p.second->numCallocCacheRefsFFL);

        fprintf (thrData.output, "num malloc instr            %lu\n",   p.second->numMallocInstrsFFL);
        fprintf (thrData.output, "num realloc instr           %lu\n", p.second->numReallocInstrsFFL);
        fprintf (thrData.output, "num calloc instr            %lu\n\n", p.second->numCallocInstrsFFL);
    }
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
		blowup_bytes += data->getBlowup();
	}

	totalMemOverhead += alignment;
	totalMemOverhead += blowup_bytes;

	totalSizeAlloc = totalMemAllocated.load();
	//Calculate metadata_overhead by using the per-object value
	uint64_t allocations = (num_malloc.load() + num_calloc.load() + num_realloc.load());
	metadata_overhead = (allocations * metadata_object);

	totalMemOverhead += metadata_overhead;
	memEfficiency = ((float) totalSizeAlloc / (totalSizeAlloc + totalMemOverhead)) * 100;
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

void collectAllocMetaData(allocation_metadata *metadata) {
	switch (metadata->type) {
		case MALLOC:
			if (!metadata->reused){
				metadata->tad->numMallocs += 1;
				metadata->tad->numMallocFaults += metadata->after.faults - metadata->before.faults;
				metadata->tad->numMallocTlbReadMisses += metadata->after.tlb_read_misses - metadata->before.tlb_read_misses;
				metadata->tad->numMallocTlbWriteMisses += metadata->after.tlb_write_misses - metadata->before.tlb_write_misses;
				metadata->tad->numMallocCacheMisses += metadata->after.cache_misses - metadata->before.cache_misses;
				metadata->tad->numMallocCacheRefs += metadata->after.cache_refs - metadata->before.cache_refs;
				metadata->tad->numMallocInstrs += metadata->after.instructions - metadata->before.instructions;
			}
			else{
				metadata->tad->numMallocsFFL += 1;
				metadata->tad->numMallocFaultsFFL += metadata->after.faults - metadata->before.faults;
				metadata->tad->numMallocTlbReadMissesFFL += metadata->after.tlb_read_misses - metadata->before.tlb_read_misses;
				metadata->tad->numMallocTlbWriteMissesFFL += metadata->after.tlb_write_misses - metadata->before.tlb_write_misses;
				metadata->tad->numMallocCacheMissesFFL += metadata->after.cache_misses - metadata->before.cache_misses;
				metadata->tad->numMallocCacheRefsFFL += metadata->after.cache_refs - metadata->before.cache_refs;
				metadata->tad->numMallocInstrsFFL += metadata->after.instructions - metadata->before.instructions;
			}
			break;
		case REALLOC:
			if (!metadata->reused){
				metadata->tad->numReallocs += 1;
				metadata->tad->numReallocFaults += metadata->after.faults - metadata->before.faults;
				metadata->tad->numReallocTlbReadMisses += metadata->after.tlb_read_misses - metadata->before.tlb_read_misses;
				metadata->tad->numReallocTlbWriteMisses += metadata->after.tlb_write_misses - metadata->before.tlb_write_misses;
				metadata->tad->numReallocCacheMisses += metadata->after.cache_misses - metadata->before.cache_misses;
				metadata->tad->numReallocCacheRefs += metadata->after.cache_refs - metadata->before.cache_refs;
				metadata->tad->numReallocInstrs += metadata->after.instructions - metadata->before.instructions;
			}
			else{
				metadata->tad->numReallocsFFL += 1;
				metadata->tad->numReallocFaultsFFL += metadata->after.faults - metadata->before.faults;
				metadata->tad->numReallocTlbReadMissesFFL += metadata->after.tlb_read_misses - metadata->before.tlb_read_misses;
				metadata->tad->numReallocTlbWriteMissesFFL += metadata->after.tlb_write_misses - metadata->before.tlb_write_misses;
				metadata->tad->numReallocCacheMissesFFL += metadata->after.cache_misses - metadata->before.cache_misses;
				metadata->tad->numReallocCacheRefsFFL += metadata->after.cache_refs - metadata->before.cache_refs;
				metadata->tad->numReallocInstrsFFL += metadata->after.instructions - metadata->before.instructions;
			}
			break;
		case CALLOC:
			if (!metadata->reused){
				metadata->tad->numCallocs += 1;
				metadata->tad->numCallocFaults += metadata->after.faults - metadata->before.faults;
				metadata->tad->numCallocTlbReadMisses += metadata->after.tlb_read_misses - metadata->before.tlb_read_misses;
				metadata->tad->numCallocTlbWriteMisses += metadata->after.tlb_write_misses - metadata->before.tlb_write_misses;
				metadata->tad->numCallocCacheMisses += metadata->after.cache_misses - metadata->before.cache_misses;
				metadata->tad->numCallocCacheRefs += metadata->after.cache_refs - metadata->before.cache_refs;
				metadata->tad->numCallocInstrs += metadata->after.instructions - metadata->before.instructions;
			}
			else{
				metadata->tad->numCallocsFFL += 1;
				metadata->tad->numCallocFaultsFFL += metadata->after.faults - metadata->before.faults;
				metadata->tad->numCallocTlbReadMissesFFL += metadata->after.tlb_read_misses - metadata->before.tlb_read_misses;
				metadata->tad->numCallocTlbWriteMissesFFL += metadata->after.tlb_write_misses - metadata->before.tlb_write_misses;
				metadata->tad->numCallocCacheMissesFFL += metadata->after.cache_misses - metadata->before.cache_misses;
				metadata->tad->numCallocCacheRefsFFL += metadata->after.cache_refs - metadata->before.cache_refs;
				metadata->tad->numCallocInstrsFFL += metadata->after.instructions - metadata->before.instructions;
			}
			break;
		default: ;
	}
}

void analyzePerfInfo(allocation_metadata *metadata) {
	Class_Size_TAD *class_size_tad;

	if(bibop){
		//If we haven't seen this thread before
		if (!threadToCSM.find(metadata->tid, &class_size_tad)){
			class_size_tad = (Class_Size_TAD*) myMalloc(sizeof(Class_Size_TAD));
			class_size_tad->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 128);
			for (int i = 0; i < num_class_sizes; i++){
				size_t cSize = class_sizes[i];
				class_size_tad->insertIfAbsent(cSize, newTad());
				allThreadsTadMap[cSize] = newTad();
			}
			threadToCSM.insertIfAbsent(metadata->tid, class_size_tad);
		}
	}
	
	//Not bibop
	else {
		if (!threadToCSM.find(metadata->tid, &class_size_tad)){
			class_size_tad = (Class_Size_TAD*) myMalloc(sizeof(Class_Size_TAD));
			class_size_tad->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 128);
			class_size_tad->insertIfAbsent(0, newTad());
			allThreadsTadMap[0] = newTad();
		}
		threadToCSM.insertIfAbsent(metadata->tid, class_size_tad);
	}

	if(d_pmuData){
			fprintf(stderr, "current class size: %lu\n", metadata->classSize);
			fprintf(stderr, "found tid -> class_size_tad\n");
	}

	if(class_size_tad->find(metadata->classSize, &(metadata->tad))){
			if(d_pmuData)
					fprintf(stderr, "found class_size_tad -> tad\n");

			collectAllocMetaData(metadata);
	}

	if (metadata->after.instructions - metadata->before.instructions != 0 && d_pmuData){
		fprintf(stderr, "Malloc from thread       %d\n"
						"From free list:          %s\n"
						"Num faults:              %ld\n"
						"Num TLB read misses:     %ld\n"
						"Num TLB write misses:    %ld\n"
						"Num cache misses:        %ld\n"
						"Num cache refs:          %ld\n"
						"Num instructions:        %ld\n\n",
				metadata->tid, metadata->reused ? "true" : "false",
				metadata->after.faults - metadata->before.faults,
				metadata->after.tlb_read_misses - metadata->before.tlb_read_misses,
				metadata->after.tlb_write_misses - metadata->before.tlb_write_misses,
				metadata->after.cache_misses - metadata->before.cache_misses,
				metadata->after.cache_refs - metadata->before.cache_refs,
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

#ifdef USE_THREAD_LOCAL
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
#endif
