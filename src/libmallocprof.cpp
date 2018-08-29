/**
 * @file libmallocprof.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 * @author Sam Silvestro <sam.silvestro@utsa.edu>
 * @author Richard Salcedo <kbgagt@gmail.com>
 */

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
//#define _GNU_SOURCE       /* See feature_test_macros(7) */

#include <dlfcn.h>
#include <malloc.h>
#include <alloca.h>
#include <sys/mman.h>
#include <sys/syscall.h>    /* For SYS_xxx definitions */
#include <unistd.h>
#include <execinfo.h>
#include <stdio.h>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "memsample.h"
#include "real.hh"
#include "xthreadx.hh"
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "spinlock.hh"
#include "selfmap.hh"
#include "Overhead.H"
#include "libmallocprof.h"

//Globals
bool bibop = false;
bool bumpPointer = false;
bool gettingClassSizes = false;
bool firstMalloc = false;
bool inGetAllocStyle = false;
bool inGetMmapThreshold = false;
bool inRealMain = false;
bool mapsInitialized = false;
bool mmap_found = false;
bool opening_maps_file = false;
bool selfmapInitialized = false;
bool usingRealloc = false;
char* allocator_name;
const char* CLASS_SIZE_FILE_NAME = "class_sizes.txt";
extern char data_start;
extern char _etext;
extern char * program_invocation_name;
extern void * __libc_stack_end;
extern char __executable_start;
FILE *classSizeFile;
float memEfficiency = 0;
initStatus mallocInitialized = NOT_INITIALIZED;
pid_t pid;
size_t alignment = 0;
size_t blowup_bytes = 0;
size_t largestClassSize;
size_t malloc_mmap_threshold = 0;
size_t metadata_object = 0;
size_t metadata_overhead = 0;
size_t totalSizeAlloc = 0;
size_t totalSizeFree = 0;
size_t totalSizeDiff = 0;
size_t totalMemOverhead = 0;
uint64_t min_pos_callsite_id;

//Debugging flags
const bool debug = false;
const bool d_malloc_info = false;
const bool d_mprotect = false;
const bool d_mmap = false;
const bool d_metadata = false;
const bool d_bibop_metadata = false;
const bool d_style = false;
const bool d_getFreelist = false;
const bool d_getClassSizes = false;
const bool d_pmuData = false;

//Atomic Globals
std::atomic<std::uint64_t> cycles_free (0);
std::atomic<std::uint64_t> cycles_new (0);
std::atomic<std::uint64_t> cycles_reused (0);
std::atomic<std::uint64_t> total_time_wait (0);
std::atomic_size_t summation_blowup_bytes (0);
std::atomic_size_t free_bytes (0);
std::atomic_size_t active_mem (0);
std::atomic_size_t active_mem_HWM (0);
std::atomic_uint num_free (0);
std::atomic_uint num_malloc (0);
std::atomic_uint num_calloc (0);
std::atomic_uint num_realloc (0);
std::atomic_uint new_address (0);
std::atomic_uint reused_address (0);
std::atomic_uint num_pthread_mutex_locks (0);
std::atomic_uint num_trylock (0);
std::atomic_uint total_waits (0);
std::atomic_uint num_dontneed (0);
std::atomic_uint num_madvise (0);
std::atomic_uint num_sbrk (0);
std::atomic_uint size_sbrk (0);
std::atomic_uint malloc_mmaps (0);
std::atomic_uint total_mmaps (0);
std::atomic_uint temp_alloc (0);
std::atomic_uint temp_position (0);
std::atomic_uint threads (0);
std::atomic_uint blowup_allocations (0);
std::atomic_uint summation_blowup_allocations (0);
std::atomic_uint num_mprotect (0);
std::atomic_uint num_operator_new (0);
std::atomic_uint num_operator_delete (0);
std::atomic_uint num_actual_malloc (0);

//Vector of class sizes
std::vector<size_t>* classSizes;

//Struct of virtual memory info
VmInfo vmInfo;

//Thread local variables
__thread thread_data thrData;
thread_local uint64_t timeAttempted;
thread_local uint64_t timeWaiting;
thread_local uint64_t numWaits;
thread_local bool waiting;
thread_local bool inAllocation;
thread_local bool inMmap;
thread_local bool samplingInit = false;

//Hashmap of thread IDs to class sizes to their thread_alloc_data structs
typedef HashMap <uint64_t, thread_alloc_data*, spinlock> classSizeMap;
HashMap <uint64_t, classSizeMap*, spinlock> tadMap;

//Hashmap of class size to tad struct, for all thread data summed up
//HashMap <uint64_t, thread_alloc_data*, spinlock> allThreadsTadMap;
std::map<uint64_t, thread_alloc_data*> allThreadsTadMap;

//Hashmap of malloc'd addresses to a ObjectTuple
HashMap <uint64_t, ObjectTuple*, spinlock> addressUsage;

//Hashmap of lock addr to LC
HashMap <uint64_t, LC*, spinlock> lockUsage;

//Hashmap of mmap addrs to tuple:
HashMap <uint64_t, MmapTuple*, spinlock> mappings;

//Hashmap of freelists, key 0 is bump pointer
typedef HashMap <uint64_t, FreeObject*, spinlock> Freelist;
HashMap <uint64_t, Freelist*, spinlock> freelistMap;

//Hashmap of Overhead objects
HashMap <size_t, Overhead*, spinlock> overhead;

//Spinlocks
spinlock temp_mem_lock;
spinlock getFreelistLock;
spinlock freelistLock;
spinlock mappingsLock;

//Functions
Freelist* getFreelist (size_t size);

// pre-init private allocator memory
char myBuffer[TEMP_MEM_SIZE];

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
	void * alloca(size_t) __attribute__ ((weak, alias("yyalloca")));
	int posix_memalign(void **, size_t, size_t) __attribute__ ((weak,
				alias("yyposix_memalign")));
}

//Constructor
__attribute__((constructor)) initStatus initializer() {
    
	if(mallocInitialized == INITIALIZED || mallocInitialized == IN_PROGRESS){
            return mallocInitialized;
    }
	
	mallocInitialized = IN_PROGRESS;

	// Ensure we are operating on a system using 64-bit pointers.
	// This is necessary, as later we'll be taking the low 8-byte word
	// of callsites. This could obviously be expanded to support 32-bit systems
	// as well, in the future.
	size_t ptrSize = sizeof(void *);
	if(ptrSize != EIGHT_BYTES) {
		fprintf(stderr, "error: unsupported pointer size: %zu\n", ptrSize);
		abort();
	}

	temp_mem_lock.init ();
	getFreelistLock.init ();
	freelistLock.init();
	mappingsLock.init();
	pid = getpid();

	// Calculate the minimum possible callsite ID by taking the low four bytes
	// of the start of the program text and repeating them twice, back-to-back,
	// resulting in eight bytes which resemble: 0x<lowWord><lowWord>
	uint64_t btext = (uint64_t)&__executable_start & 0xFFFFFFFF;
	min_pos_callsite_id = (btext << 32) | btext;

	RealX::initializer();

    tadMap.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
    //allThreadsTadMap.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	addressUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	lockUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	mappings.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	freelistMap.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	overhead.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);

	classSizes = new std::vector<size_t>();
    allocator_name = (char *) myMalloc(100 * sizeof(char));

	void * program_break = sbrk(0);
	thrData.stackStart = (char *)__builtin_frame_address(0);
	thrData.stackEnd = (char *)__libc_stack_end;

	selfmap::getInstance().getTextRegions();
	selfmapInitialized = true;
	
	// Determines allocator style: bump pointer or bibop
	getAllocStyle();

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

	// Determine class size for bibop allocator
	// If the class sizes are in the class_sizes.txt file
	// it will read from that. Else if will get the info
	if (bibop) {
		//This function also assigns elements to:
		//freelist hashmap
		//overhead hashmap
		getClassSizes ();
	}
	else {
		//Find the malloc_mmap_threshold
		getMmapThreshold();

		//Create the freelist for small objects
		Freelist* small = (Freelist*) myMalloc (sizeof (Freelist));
		small->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
		freelistMap.insert(0, small);

		//Create the freelist for objects >= 512 Bytes
		Freelist* large = (Freelist*) myMalloc (sizeof (Freelist));
		large->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
		freelistMap.insert(LARGE_OBJECT, large);

		//Find metadata size for bump pointer allocator
		get_bp_metadata();

		//Create an entry in the overhead hashmap with key 0
		overhead.insert(0, new Overhead());
	}

	mapsInitialized = true;
	mallocInitialized = INITIALIZED;
	getMemUsageStart();

	return mallocInitialized;
}

__attribute__((destructor)) void finalizer_mallocprof () {
	fclose(thrData.output);
}

void exitHandler() {

	inRealMain = false;
	doPerfRead();
	getMemUsageEnd();
	calculateMemOverhead();
	fflush(thrData.output);
    globalizeThreadAllocData();
	writeAllocData();
	writeContention();
}

// MallocProf's main function
int libmallocprof_main(int argc, char ** argv, char ** envp) {

   // Register our cleanup routine as an on-exit handler.
	atexit(exitHandler);
    if(!samplingInit){
	    initSampling();
        samplingInit = true;
    }
	
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
		if((mallocInitialized != INITIALIZED) || (!selfmapInitialized)) {
			if((temp_position + sz) < TEMP_MEM_SIZE) 
				return myMalloc (sz);
			else {
				fprintf(stderr, "error: temp allocator out of memory\n");
				fprintf(stderr, "\t requested size = %zu, total size = %d, total allocs = %u\n",
						  sz, TEMP_MEM_SIZE, temp_alloc.load(relaxed));
				abort();
			}
		}
		
		//Malloc is being called by a thread that is already in malloc
		if (inAllocation) return RealX::malloc (sz);

		//If in our getClassSizes function, use RealX
		//We need this because the vector uses push_back
		//which calls malloc, also fopen calls malloc
		if(gettingClassSizes) return RealX::malloc(sz);

        if(!samplingInit){
            initSampling();
            samplingInit = true;
        }

		if (!inRealMain) return RealX::malloc(sz);
	
		//thread_local
		inAllocation = true;

        num_malloc.fetch_add(1, relaxed);

		//Data we need for each allocation
		bool reused = false;
		pid_t tid = gettid();
		PerfReadInfo before;
		PerfReadInfo after;
		size_t classSize = getClassSizeFor(sz);
		uint64_t cyclesForMalloc = 0;
		uint64_t address = 0;
		uint64_t tsc_before = 0;
		uint64_t tsc_after = 0;
		void* object;

		//thread_local
		inAllocation = true;

		//Do before
		doBefore(&before, &tsc_before);

		//Do allocation
		object = RealX::malloc(sz);
		
		//Do after
		doAfter(&after, &tsc_after);

        cyclesForMalloc = tsc_after - tsc_before;
		address = reinterpret_cast <uint64_t> (object);

		//Gets overhead, address usage, mmap usage, memHWM
		analyzeAllocation(sz, address, cyclesForMalloc, classSize, &reused);

		//Get perf info
		analyzePerfInfo(&before, &after, classSize, &reused, tid);

		//thread_local
		inAllocation = false;

		return object;
	}

	void * yycalloc(size_t nelem, size_t elsize) {

		if (mallocInitialized != INITIALIZED) {

			void * ptr = NULL;
			ptr = yymalloc (nelem * elsize);
			if (ptr) memset(ptr, 0, nelem * elsize);
			return ptr;
		}
		
		if (inAllocation) {
			return RealX::calloc(nelem, elsize);
		}

        if(!samplingInit){
            initSampling();
            samplingInit = true;
        }

		if (!inRealMain) return RealX::calloc (nelem, elsize);
		
        //thread_local
		inAllocation = true;

		num_calloc.fetch_add(1, relaxed);

		//Data we need for each allocation
		bool reused = false;
		pid_t tid = gettid();
		PerfReadInfo before;
		PerfReadInfo after;
		size_t sz = nelem*elsize;
		size_t classSize = getClassSizeFor(sz);
		uint64_t cyclesForCalloc = 0;
		uint64_t address = 0;
		uint64_t tsc_before = 0;
		uint64_t tsc_after = 0;
		void* object;

		//Do before
		doBefore(&before, &tsc_before);

		//Do allocation
		object = RealX::calloc(nelem, elsize);
	
		//Do after
		doAfter(&after, &tsc_after);
		
        cyclesForCalloc = tsc_after - tsc_before;
		address = reinterpret_cast <uint64_t> (object);

		//Gets overhead, address usage, mmap usage, memHWM
		analyzeAllocation(sz, address, cyclesForCalloc, classSize, &reused);

		//Get perf info
		analyzePerfInfo(&before, &after, classSize, &reused, tid);

		//thread_local
		inAllocation = false;

		return object;
	}

	void yyfree(void * ptr) {
		if(ptr == NULL)
			return;

        if(!samplingInit){
            initSampling();
            samplingInit = true;
        }

		// Determine whether the specified object came from our global buffer;
		// only call RealX::free() if the object did not come from here.
		if ((ptr >= (void *) myBuffer) && (ptr <= ((void *)(myBuffer + TEMP_MEM_SIZE)))) {
			myFree (ptr);
			return;
		}

		if (mallocInitialized != INITIALIZED) {
			RealX::free (ptr);
			return;
		}

		if (!inRealMain) {
			RealX::free (ptr);
			return;
		}

		//Data we need for each allocation
        classSizeMap *csm;
		PerfReadInfo before;
		PerfReadInfo after;
        thread_alloc_data *tad;
		size_t currentClassSize = 0;
        uint64_t tid = gettid();
		uint64_t tsc_before = 0;
		uint64_t tsc_after = 0;
		uint64_t size = 0;
		uint64_t address = reinterpret_cast <uint64_t> (ptr);

		currentClassSize = getClassSizeFor(size);

		//Do before free
		doBefore(&before, &tsc_before);
		
		//Do free
		RealX::free(ptr);
		
		//Do after free
		doAfter(&after, &tsc_after);

		//Insert object into our freelist
		analyzeFree(address);

		//Update atomics
		cycles_free.fetch_add ((tsc_after - tsc_before), relaxed);
		num_free.fetch_add(1, relaxed);
        
		csm = nullptr;
		if(tadMap.find(tid, &csm)){
			if(d_pmuData){
				fprintf(stderr, "current class size: %lu\n", currentClassSize);
				fprintf(stderr, "found tid -> csm\n");
			}

			if(csm->find(currentClassSize, &tad)){
				if(d_pmuData)
					fprintf(stderr, "found csm -> tad\n");

				tad->numFrees += 1;
				tad->numFreeFaults += after.faults - before.faults;
				tad->numFreeTlbMisses += after.tlb_misses - before.tlb_misses;
				tad->numFreeCacheMisses += after.cache_misses - before.cache_misses;
				tad->numFreeCacheRefs += after.cache_refs - before.cache_refs;
				tad->numFreeInstrs += after.instructions - before.instructions;
			}
		}

		if (((after.instructions - before.instructions) != 0) && d_pmuData){
			fprintf(stderr, "Free from thread %lu\n"
							"Num faults:        %ld\n"
							"Num TLB misses:    %ld\n"
							"Num cache misses:  %ld\n"
							"num cache refs:    %ld\n"
							"Num instructions:  %ld\n\n", 
			tid, after.faults - before.faults, after.tlb_misses - before.tlb_misses,
			after.cache_misses - before.cache_misses, after.cache_refs - before.cache_refs,
			after.instructions - before.instructions);
		}

		free_bytes += size;
		active_mem -= size;
	}

	void * yyrealloc(void * ptr, size_t sz) {

		if (!mapsInitialized) return RealX::realloc (ptr, sz);

		if (inAllocation) return RealX::realloc (ptr, sz);

		if(mallocInitialized != INITIALIZED) {
			if(ptr == NULL) return yymalloc(sz);

			yyfree(ptr);
			return yymalloc(sz);
		}

        if(!samplingInit){
            initSampling();
            samplingInit = true;
        }
		
		if (!inRealMain) return RealX::realloc (ptr, sz);

		num_realloc.fetch_add(1, relaxed);

		//Data we need for each allocation
		bool reused = false;
		pid_t tid = gettid();
		PerfReadInfo before;
		PerfReadInfo after;
		size_t classSize = getClassSizeFor(sz);
		uint64_t cyclesForRealloc = 0;
		uint64_t address = 0;
		uint64_t tsc_before = 0;
		uint64_t tsc_after = 0;
		void* object;

		//thread_local
		inAllocation = true;

		//Do before
		doBefore(&before, &tsc_before);

		//Do allocation
		object = RealX::realloc(ptr, sz);

		//Do after
		doAfter(&after, &tsc_after);

        cyclesForRealloc = tsc_after - tsc_before;
		address = reinterpret_cast <uint64_t> (object);

		//Gets overhead, address usage, mmap usage, memHWM
		analyzeAllocation(sz, address, cyclesForRealloc, classSize, &reused);

		//Get perf info
		analyzePerfInfo(&before, &after, classSize, &reused, tid);

		//thread_local
		inAllocation = false;

		return object;
	}

	inline void logUnsupportedOp() {
		fprintf(thrData.output,
				"ERROR: call to unsupported memory function: %s\n",
				__FUNCTION__);
	}
	void * yyalloca(size_t size) {
		logUnsupportedOp();
		return NULL;
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

	// Intercept thread creation
	int pthread_create(pthread_t * tid, const pthread_attr_t * attr,
							 void *(*start_routine)(void *), void * arg) {

		int result = xthreadx::thread_create(tid, attr, start_routine, arg);
		threads.fetch_add(1, relaxed);
		return result;
	}

	int pthread_join(pthread_t thread, void **retval) {

		int result = RealX::pthread_join (thread, retval);
        //fprintf(stderr, "Thread: %lX\n", thread);
		return result;
	}

	int pthread_mutex_lock(pthread_mutex_t *mutex) {
	
		if (!mapsInitialized) 
			return RealX::pthread_mutex_lock (mutex);

		uint64_t lockAddr = reinterpret_cast <uint64_t> (mutex);

		//Is this thread doing allocation
		if (inAllocation) {

			//Have we encountered this lock before?
			LC* thisLock;
			if (lockUsage.find (lockAddr, &thisLock)) {
				thisLock->contention++;

				if (thisLock->contention > thisLock->maxContention)
					thisLock->maxContention = thisLock->contention;

				if (thisLock->contention > 1) {
				   timeAttempted = rdtscp();
					numWaits++;
					total_waits.fetch_add(1, relaxed);
					waiting = true;
				}
			}	

			//Add lock to lockUsage hashmap
			else {
				num_pthread_mutex_locks.fetch_add(1, relaxed);
				lockUsage.insertIfAbsent (lockAddr, newLC());
			}	
		}

		//Aquire the lock
		int result = RealX::pthread_mutex_lock (mutex);
		if (waiting) {
			timeWaiting += ((rdtscp()) - timeAttempted);
			waiting = false;
			total_time_wait.fetch_add (timeWaiting, relaxed);
		}
		return result;
	}

	int pthread_mutex_trylock (pthread_mutex_t *mutex) {

		if (!mapsInitialized)
			return RealX::pthread_mutex_trylock (mutex);
	
		uint64_t lockAddr = reinterpret_cast <uint64_t> (mutex);

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
		}
		return result;
	}

	int pthread_mutex_unlock(pthread_mutex_t *mutex) {

		if (!mapsInitialized)
			return RealX::pthread_mutex_unlock (mutex);

		uint64_t lockAddr = reinterpret_cast <uint64_t> (mutex);

		//Decrement contention on this LC if this lock is
		//in our map
		LC* thisLock;
		if (lockUsage.find (lockAddr, &thisLock)) {
			thisLock->contention--;
		}

		return RealX::pthread_mutex_unlock (mutex);
	}

	int madvise(void *addr, size_t length, int advice){

		if (advice == MADV_DONTNEED)
			num_dontneed.fetch_add(1, relaxed);

		num_madvise.fetch_add(1, relaxed);
		return RealX::madvise(addr, length, advice);
	}

    void *sbrk(intptr_t increment){
        if(mallocInitialized != INITIALIZED) return RealX::sbrk(increment);

        void *retptr = RealX::sbrk(increment);
        uint64_t newProgramBreak = (uint64_t) RealX::sbrk(0);
        uint64_t oldProgramBreak = (uint64_t) retptr;
        uint64_t sizeChange = newProgramBreak - oldProgramBreak;

        size_sbrk.fetch_add(sizeChange, relaxed);
        num_sbrk.fetch_add(1, relaxed);

        return retptr;
    }

	int mprotect (void* addr, size_t len, int prot) {

		num_mprotect.fetch_add(1, relaxed);
		if (d_mprotect)
			printf ("mprotect/found= %s, addr= %p, len= %zu, prot= %d\n",
		mappingEditor(addr, len, prot) ? "true" : "false", addr, len, prot);

		return RealX::mprotect (addr, len, prot);
	}

	void * yymmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {

		if((initializer() == IN_PROGRESS) && !inGetMmapThreshold && !inGetAllocStyle)
			return RealX::mmap(addr, length, prot, flags, fd, offset);

		if (inMmap) return RealX::mmap (addr, length, prot, flags, fd, offset);

		inMmap = true;
		void* retval = RealX::mmap(addr, length, prot, flags, fd, offset);
		uint64_t address = reinterpret_cast <uint64_t> (retval);

		//Is the profiler getting class sizes
		if (inGetMmapThreshold) {

			malloc_mmap_threshold = length;
			mmap_found = true;
			inMmap = false;
			return retval;
		}

		//If this thread currently doing an allocation
		if (inAllocation) {
			if (d_mmap) printf ("mmap direct from allocation function: length= %zu, prot= %d\n", length, prot);
			malloc_mmaps.fetch_add (1, relaxed);
			mappingsLock.lock();
			mappings.insert(address, newMmapTuple(address, length, prot, 'a'));
			mappingsLock.unlock();
		}

		//Need to check if selfmap.getInstance().getTextRegions() has
		//ran. If it hasn't, we can't call isAllocatorInCallStack()
		else if (selfmapInitialized && isAllocatorInCallStack()) {
			if (d_mmap) printf ("mmap allocator in callstack: length= %zu, prot= %d\n", length, prot);
			mappingsLock.lock();
			mappings.insert(address, newMmapTuple(address, length, prot, 's'));
			mappingsLock.unlock();
		}
		else {
			if (d_mmap) printf ("mmap from unknown source: length= %zu, prot= %d\n", length, prot);
			mappingsLock.lock();
			mappings.insert(address, newMmapTuple(address, length, prot, 'u'));
			mappingsLock.unlock();
		}

		total_mmaps++;

		inMmap = false;
		return retval;
	}
}//End of extern "C"

void analyzeAllocation(size_t size, uint64_t address, uint64_t cycles, size_t classSize, bool* reused) {

	//Analyzes for alignment and blowup
	getOverhead(size, address, classSize, reused);

	//Analyze address usage
	getAddressUsage(size, address, classSize, cycles);

	//Update mmap region info
	getMappingsUsage(size, address, classSize);

	//Increase "active mem"
	increaseMemoryHWM(size);
}

void analyzeFree(uint64_t address) {

	ObjectTuple* t;
	size_t size = 0;

	if (addressUsage.find(address, &t)){
		size = t->szUsed;
		t->szFreed += size;
		t->numAccesses++;
		t->numFrees++;

		//Add this object to it's Freelist
		//If bump pointer, add to freelistMap[0]
		if (size <= malloc_mmap_threshold) {
			freelistLock.lock();
			if (!bibop) {
				if (size < LARGE_OBJECT) {
					Freelist* f;
					if (freelistMap.find(0, &f)) {
						(*f).insert(address, newFreeObject(address, size));
					}
					else {
						printf ("Freelist[0] not found.\n");
						abort ();
					}
				}
				else {
					Freelist* f;
					if (freelistMap.find(LARGE_OBJECT, &f)) {
						(*f).insert(address, newFreeObject(address, size));
					}
					else {
						printf ("Freelist[LARGE_OBJECT] not found.\n");
						abort();
					}
				}
			}

			//If bibop, add to freelistMap[size]
			else {
				Freelist* f = getFreelist(size);
				(*f).insert(address, newFreeObject(address, size));
			}
			freelistLock.unlock();
		}
	}
}

void increaseMemoryHWM(size_t size) {

	uint32_t n = active_mem += size;
	if ((n + size) > active_mem_HWM) {
		active_mem_HWM = (n + size);
		if (d_malloc_info) printf ("increasing a_m_HWM, n= %u, size= %zu\n", n, size);
	}
}

void getAddressUsage(size_t size, uint64_t address, size_t classSize, uint64_t cycles) {

	ObjectTuple* t;
	//Has this address been used before
	if (addressUsage.find (address, &t)) {
		t->numAccesses++;
		t->szTotal += size;
		t->numAllocs++;
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

	mappingsLock.lock();
	for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
		auto data = entry.getData();
		if (data->start <= address && address <= data->end) {
			data->allocations.fetch_add(1, relaxed);
			break;
		}
	}
	mappingsLock.unlock();
}

void getOverhead (size_t size, uint64_t address, size_t classSize, bool* reused) {

	//If size is greater than malloc_mmap_threshold
	//Then it will be mmap'd. So no need to check
	if (size <= malloc_mmap_threshold) {

		//Check for classSize alignment bytes
		if (bibop) getAlignment(size, classSize);

		//Check for memory blowup
		getBlowup(size, address, classSize, reused);
		
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

void getBlowup (size_t size, uint64_t address, size_t classSize, bool* reused) {

	freelistLock.lock();
	Freelist* freelist;
	bool reuseFreePossible = false;
	bool reuseFreeObject = false;

	if (!bibop) {
		if (size < LARGE_OBJECT) {

			if (freelistMap.find(0, &freelist)) {
				for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
					auto data = f.getData();
					if (size <= data->size) {
						reuseFreePossible = true;
					}
					if (address == data->addr) {
						reuseFreeObject = true;
						(*freelist).erase(address);
						free_bytes -= size;
						break;
					}
				}
			}
			else {
				printf ("Freelist[0] not found.\n");
				abort();
			}
		}
		else {
			if (freelistMap.find(LARGE_OBJECT, &freelist)) {
				for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
					auto data = f.getData();
					if (size <= data->size) {
						reuseFreePossible = true;
					}
					if (address == data->addr) {
						reuseFreeObject = true;
						(*freelist).erase(address);
						free_bytes -= size;
						break;
					}
				}
			}
			else {
				printf ("Freelist[LARGE_OBJECT] not found.\n");
				abort();
			}
		}
	}

	else {
		freelist = getFreelist(size);
		if ((*freelist).begin() != (*freelist).end()) {
			reuseFreePossible = true;
			for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
				auto data = f.getData();
				if (address == data->addr) {
					reuseFreeObject = true;
					(*freelist).erase(address);
					free_bytes -= size;
					break;
				}
			}
		}
	}

	//If Reuse was possible, but didn't reuse
	if (reuseFreePossible && !reuseFreeObject) {
		//possible memory blowup has occurred
		//get size of allocation
		Overhead* o;
		if (overhead.find(classSize, &o)) {
			o->addBlowup(size);
		}
		blowup_allocations.fetch_add(1, relaxed);
	}

	freelistLock.unlock();

	//If there is enough free bytes to satisfy the allocation
	//Regardless of where they came from, such as memory that
	//was freed during intialization or setup, and not necessarily
	//from objects freed within the main function
	if (size <= free_bytes) {
		summation_blowup_bytes.fetch_add(size, relaxed);
		summation_blowup_allocations.fetch_add(1, relaxed);
	}
	*reused = reuseFreeObject;
}

//Return the appropriate class size that this object should be in
size_t getClassSizeFor (size_t size) {

	size_t sizeToReturn = 0;
	if (bibop) {
		if (classSizes->empty()) {
			fprintf (stderr, "classSizes is empty. Abort()\n");
			abort();
		}

		for (auto element = classSizes->begin(); element != classSizes->end(); element++) {
			size_t tempSize = *element;
			if (size > tempSize) continue;
			else if (size <= tempSize) {
				sizeToReturn = tempSize;
				break;
			}
		}
	}
	return sizeToReturn;
}

//I pulled these from OpenBSD's wrapper.cpp file
//Changed OBSD's "CUSTOM_MALLOC" and "CUSTOM_FREE"
//to our functions
#ifndef NEW_INCLUDED
#define NEW_INCLUDED

void * operator new (size_t sz)
#if defined(_GLIBCXX_THROW)
	_GLIBCXX_THROW (std::bad_alloc)
#endif
{
	void * ptr = yymalloc (sz);
	if (ptr == NULL) {
		throw std::bad_alloc();
	} else {
		return ptr;
	}
}

void operator delete (void * ptr)
#if !defined(linux_)
	throw ()
#endif
{
	yyfree (ptr);
}

#if !defined(__SUNPRO_CC) || __SUNPRO_CC > 0x420
void * operator new (size_t sz, const std::nothrow_t&) throw() {
	return yymalloc(sz);
}

void * operator new[] (size_t size)
#if defined(_GLIBCXX_THROW)
	_GLIBCXX_THROW (std::bad_alloc)
#endif
{
	//I don't want ifstream's constructor to initialize
	//the allocator. So satisfy through myMalloc
	if (opening_maps_file) {
		void* p = myMalloc(size);
		return p;
	}
	void * ptr = yymalloc(size);
	if (ptr == NULL) {
		throw std::bad_alloc();
	} else {
		return ptr;
	}
}

void * operator new[] (size_t sz, const std::nothrow_t&)
throw()
{
	return yymalloc(sz);
}

void operator delete[] (void * ptr)
#if defined(_GLIBCXX_USE_NOEXCEPT)
	_GLIBCXX_USE_NOEXCEPT
#else
#if defined(__GNUC__)
	// clang + libcxx on linux
	_NOEXCEPT
#endif
#endif
{
	yyfree (ptr);
}

#if defined(__cpp_sized_deallocation) && __cpp_sized_deallocation >= 201309

void operator delete(void * ptr, size_t)
#if !defined(linux_)
	throw ()
#endif
{
	yyfree (ptr);
}

void operator delete[](void * ptr, size_t)
#if defined(__GNUC__)
	_GLIBCXX_USE_NOEXCEPT
#endif
{
	yyfree (ptr);
}
#endif

#endif
#endif

// Create tuple for hashmap
ObjectTuple* newObjectTuple (uint64_t address, size_t size) {

	ObjectTuple* t = (ObjectTuple*) myMalloc (sizeof (ObjectTuple));	
	
	t->addr = address;
	t->numAccesses = 1;
	t->szTotal = size;
	t->szUsed = size;
	t->szFreed = 0;
	t->numAllocs = 1;
	t->numFrees = 0;

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

LC* newLC () {
	LC* lc = (LC*) myMalloc (sizeof(LC));
	lc->contention = 1;
	lc->maxContention = 1;
	return lc;
}

thread_alloc_data* newTad(){
    thread_alloc_data* tad = (thread_alloc_data*) myMalloc (sizeof(thread_alloc_data));
    
    tad->numMallocFaults = 0;
    tad->numReallocFaults = 0;
    tad->numFreeFaults = 0;

    tad->numMallocTlbMisses = 0;
    tad->numReallocTlbMisses = 0;
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

    tad->numMallocTlbMissesFFL = 0;
    tad->numReallocTlbMissesFFL = 0;

    tad->numMallocCacheMissesFFL = 0;
    tad->numReallocCacheMissesFFL = 0;

    tad->numMallocCacheRefsFFL = 0;
    tad->numReallocCacheRefsFFL = 0;

    tad->numMallocInstrsFFL = 0;
    tad->numReallocInstrsFFL = 0;
    
    return tad;
}

FreeObject* newFreeObject (uint64_t addr, uint64_t size) {

	FreeObject* f = (FreeObject*) myMalloc (sizeof (FreeObject));
	f->addr = addr;
	f->size = size;
	return f;
}

void* myMalloc (size_t size) {

	temp_mem_lock.lock ();
	void* retptr;
	if((temp_position + size) < TEMP_MEM_SIZE) {
		retptr = (void *)(myBuffer + temp_position);
		temp_position.fetch_add (size, relaxed);
		temp_alloc++;
	} else {
		fprintf(stderr, "error: global allocator out of memory\n");
		fprintf(stderr, "\t requested size = %zu, total size = %d, "
							 "total allocs = %u\n", size, TEMP_MEM_SIZE, temp_alloc.load(relaxed));
		abort();
	}
	temp_mem_lock.unlock ();
	return retptr;
}

void myFree (void* ptr) {

	if (ptr == NULL) return;	

	temp_mem_lock.lock();
	if ((ptr >= (void*)myBuffer) && (ptr <= ((void*)(myBuffer + TEMP_MEM_SIZE)))) {
		temp_alloc--;
		if(temp_alloc == 0) temp_position = 0;
	}
	temp_mem_lock.unlock();
}

void getMemUsageStart () {

	std::ifstream file ("/proc/self/status");
	std::string line;

	for (int i = 0; i <= 41; i++) {

		std::getline (file, line);
		if (line.find("VmSize") != std::string::npos) {
			std::sscanf (line.data(), "%*s%zu%*s", &vmInfo.VmSize_start);
		}
		
		else if (line.find("VmRSS") != std::string::npos) {
			std::sscanf (line.data(), "%*s%zu%*s", &vmInfo.VmRSS_start);
		}

		else if (line.find("VmLib") != std::string::npos) {
			std::sscanf (line.data(), "%*s%zu%*s", &vmInfo.VmLib);
		}
	}

	file.close();
}

void getMemUsageEnd () {

	std::ifstream file ("/proc/self/status");
	std::string line;

	for (int i = 0; i <= 41; i++) {

		std::getline (file, line);
		if (line.find("VmSize") != std::string::npos) {
			std::sscanf (line.data(), "%*s%zu%*s", &vmInfo.VmSize_end);
		}
		
		else if (line.find("VmRSS") != std::string::npos) {
			std::sscanf (line.data(), "%*s%zu%*s", &vmInfo.VmRSS_end);
		}

		else if (line.find("VmPeak") != std::string::npos) {
			std::sscanf (line.data(), "%*s%zu%*s", &vmInfo.VmPeak);
		}

		else if (line.find("VmHWM") != std::string::npos) {
			std::sscanf (line.data(), "%*s%zu%*s", &vmInfo.VmHWM);
		}
	}

	file.close();
}

// Try to figure out which allocator is being used
void getAllocStyle () {

	inGetAllocStyle = true;
	//Should be first malloc that initializes allocator
	void* add1 = RealX::malloc (128);

	//Check for mmap'd metadata
	if (total_mmaps > 0) {
		if (d_style) printf ("mmaps > 0. get_bibop_metadata()...\n");
		get_bibop_metadata();
	}
	else {
		if (d_style) printf ("total_mmaps <= 0\n");
	}

	void* add2 = RealX::malloc (2048);

	long address1 = reinterpret_cast<long> (add1);
	long address2 = reinterpret_cast<long> (add2);

	RealX::free (add1);
	RealX::free (add2);

	long address1Page = address1 / 4096;
	long address2Page = address2 / 4096;

	if ((address1Page - address2Page) != 0) {bibop = true;}
	else {bumpPointer = true;}

	inGetAllocStyle = false;
}

void getClassSizes () {

	size_t bytesToRead = 0;
	bool matchFound = false;
	gettingClassSizes = true;
	char *line;
	char *token;

	allocator_name = strrchr(allocator_name, '/') + 1;        
	classSizeFile = fopen(CLASS_SIZE_FILE_NAME, "a+");        

	while(getline(&line, &bytesToRead, classSizeFile) != -1){

		line[strcspn(line, "\n")] = 0;
		if(strcmp(line, allocator_name) == 0) {
			matchFound = true;
			break;
		}
	}
        
	if (matchFound){
		if (debug) printf ("match found\n");
		getline(&line, &bytesToRead, classSizeFile);
        line[strcspn(line, "\n")] = 0;
                
        while( (token = strsep(&line, " ")) ){
			if(atoi(token) != 0){                        
				classSizes->push_back( (uint64_t) atoi(token));
			}
		}
		malloc_mmap_threshold = *(classSizes->end() - 1);
   }
        
   if(!matchFound){
            
		if (debug) printf ("match not found\n");
		getMmapThreshold();
        void* oldAddress;
		void* newAddress;
		uint64_t oldAddr = 0, newAddr = 0;
		size_t oldSize = 8, newSize = 8;

		oldAddress = RealX::malloc (oldSize);
		oldAddr = reinterpret_cast <uint64_t> (oldAddress);

		while (newSize <= malloc_mmap_threshold) {

			newSize += 8;
			newAddress = RealX::realloc (oldAddress, newSize);
			newAddr = reinterpret_cast <uint64_t> (newAddress);
			if (newAddr != oldAddr) {
				classSizes->push_back (oldSize);
			}

			oldAddr = newAddr;
			oldAddress = newAddress;
			oldSize = newSize;
		}

		RealX::free (newAddress);

		fprintf(classSizeFile, "%s\n", allocator_name);

		for (auto cSize = classSizes->begin(); cSize != classSizes->end(); cSize++) 
			fprintf (classSizeFile, "%zu ", *cSize);

		fprintf(classSizeFile, "\n");
		fflush (classSizeFile);
   }

	fprintf (thrData.output, ">>> classSizes    ");

	if (!classSizes->empty()) {

		for (auto cSize = classSizes->begin(); cSize != classSizes->end(); cSize++) 
			fprintf (thrData.output, "%zu ", *cSize);
	}

	fclose(classSizeFile);
	fprintf (thrData.output, "\n");
	fflush (thrData.output);

	largestClassSize = *(classSizes->end() - 1);

	int i = 0;
	for (auto cSize = classSizes->begin(); cSize != classSizes->end(); cSize++) {
		auto classSize = *cSize;
		Freelist* f = (Freelist*) myMalloc (sizeof (Freelist));
		f->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
		freelistMap.insert(classSize, f);
		if (d_getClassSizes) printf ("initializing overhead hashmap. key %d is %zu\n", i, classSize);
		overhead.insert(classSize, new Overhead());
		i++;
	}

	gettingClassSizes = false;
}

void getMmapThreshold () {

	inGetMmapThreshold = true;
	size_t size = 3000;
	void* mallocPtr;

	// Find malloc mmap threshold
	size = 3000;
	while (!mmap_found) {

		mallocPtr = RealX::malloc (size);
		RealX::free (mallocPtr);
		size += 8;
	}

	inGetMmapThreshold = false;
}

void globalizeThreadAllocData(){
    //HashMap<uint64_t, thread_alloc_data*, spinlock>::iterator i;    
    for(auto it1 = tadMap.begin(); it1 != tadMap.end(); it1++){
        for(auto it2 = it1.getData()->begin(); it2 != it1.getData()->end(); it2++){

            allThreadsTadMap[it2.getKey()]->numMallocs += it2.getData()->numMallocs;
            allThreadsTadMap[it2.getKey()]->numReallocs += it2.getData()->numReallocs;
            allThreadsTadMap[it2.getKey()]->numFrees += it2.getData()->numFrees;
            
            allThreadsTadMap[it2.getKey()]->numMallocsFFL += it2.getData()->numMallocsFFL;
            allThreadsTadMap[it2.getKey()]->numReallocsFFL += it2.getData()->numReallocsFFL;

            allThreadsTadMap[it2.getKey()]->numMallocFaults += it2.getData()->numMallocFaults;
            allThreadsTadMap[it2.getKey()]->numReallocFaults += it2.getData()->numReallocFaults;
            allThreadsTadMap[it2.getKey()]->numFreeFaults += it2.getData()->numFreeFaults;
            
            allThreadsTadMap[it2.getKey()]->numMallocTlbMisses += it2.getData()->numMallocTlbMisses;
            allThreadsTadMap[it2.getKey()]->numReallocTlbMisses += it2.getData()->numReallocTlbMisses;
            allThreadsTadMap[it2.getKey()]->numFreeTlbMisses += it2.getData()->numFreeTlbMisses;

            allThreadsTadMap[it2.getKey()]->numMallocCacheMisses += it2.getData()->numMallocCacheMisses;
            allThreadsTadMap[it2.getKey()]->numReallocCacheMisses += it2.getData()->numReallocCacheMisses;
            allThreadsTadMap[it2.getKey()]->numFreeCacheMisses += it2.getData()->numFreeCacheMisses;

            allThreadsTadMap[it2.getKey()]->numMallocCacheRefs += it2.getData()->numMallocCacheRefs;
            allThreadsTadMap[it2.getKey()]->numReallocCacheRefs += it2.getData()->numReallocCacheRefs;
            allThreadsTadMap[it2.getKey()]->numFreeCacheRefs += it2.getData()->numFreeCacheRefs;

            allThreadsTadMap[it2.getKey()]->numMallocInstrs += it2.getData()->numMallocInstrs;
            allThreadsTadMap[it2.getKey()]->numReallocInstrs += it2.getData()->numReallocInstrs;
            allThreadsTadMap[it2.getKey()]->numFreeInstrs += it2.getData()->numFreeInstrs;

            allThreadsTadMap[it2.getKey()]->numMallocFaultsFFL += it2.getData()->numMallocFaultsFFL;
            allThreadsTadMap[it2.getKey()]->numReallocFaultsFFL += it2.getData()->numReallocFaultsFFL;
            
            allThreadsTadMap[it2.getKey()]->numMallocTlbMissesFFL += it2.getData()->numMallocTlbMissesFFL;
            allThreadsTadMap[it2.getKey()]->numReallocTlbMissesFFL += it2.getData()->numReallocTlbMissesFFL;
            
            allThreadsTadMap[it2.getKey()]->numMallocCacheMissesFFL += it2.getData()->numMallocCacheMissesFFL;
            allThreadsTadMap[it2.getKey()]->numReallocCacheMissesFFL += it2.getData()->numReallocCacheMissesFFL;
            
            allThreadsTadMap[it2.getKey()]->numMallocCacheRefsFFL += it2.getData()->numMallocCacheRefsFFL;
            allThreadsTadMap[it2.getKey()]->numReallocCacheRefsFFL += it2.getData()->numReallocCacheRefsFFL;
            
            allThreadsTadMap[it2.getKey()]->numMallocInstrsFFL += it2.getData()->numMallocInstrsFFL;
            allThreadsTadMap[it2.getKey()]->numReallocInstrsFFL += it2.getData()->numReallocInstrsFFL;
        /*
            allThreadsNumMallocFaults.fetch_add(it2.getData()->numMallocFaults);
            allThreadsNumReallocFaults.fetch_add(it2.getData()->numReallocFaults);
            allThreadsNumFreeFaults.fetch_add(it2.getData()->numFreeFaults);

            allThreadsNumMallocTlbMisses.fetch_add(it2.getData()->numMallocTlbMisses);
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
	unsigned print_new_address = new_address.load(relaxed);
	unsigned print_reused_address = reused_address.load(relaxed);
	unsigned print_num_free = num_free.load(relaxed);
	unsigned print_total_waits = total_waits.load(relaxed);

	fprintf(thrData.output, "\n");
	fprintf(thrData.output, ">>> num_malloc               %u\n", num_malloc.load(relaxed));
	fprintf(thrData.output, ">>> num_actual_malloc        %u\n", num_actual_malloc.load(relaxed));
	fprintf(thrData.output, ">>> num_calloc               %u\n", num_calloc.load(relaxed));
	fprintf(thrData.output, ">>> num_realloc              %u\n", num_realloc.load(relaxed));
	fprintf(thrData.output, ">>> num_operator_new         %u\n", num_operator_new.load(relaxed));
	fprintf(thrData.output, ">>> new_address              %u\n", print_new_address);
	fprintf(thrData.output, ">>> reused_address           %u\n", print_reused_address);
	fprintf(thrData.output, ">>> num_free                 %u\n", print_num_free);
	fprintf(thrData.output, ">>> num_operator_delete      %u\n", num_operator_delete.load(relaxed));
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
	fprintf(thrData.output, ">>> total_waits              %u\n", print_total_waits);
	fprintf(thrData.output, ">>> num_trylock              %u\n", num_trylock.load(relaxed));

	if (print_total_waits) 
	fprintf(thrData.output, ">>> avg_wait_time            %lu\n",
							(total_time_wait.load(relaxed) / print_total_waits));

	fprintf(thrData.output, ">>> num_madvise              %u\n", num_madvise.load(relaxed));
    fprintf(thrData.output, ">>> num_dontneed             %u\n", num_dontneed.load(relaxed));
	fprintf(thrData.output, ">>> num_sbrk                 %u\n", num_sbrk.load(relaxed));
	fprintf(thrData.output, ">>> size_sbrk                %u\n", size_sbrk.load(relaxed));
	fprintf(thrData.output, ">>> VmSize_start(kB)         %zu\n", vmInfo.VmSize_start);
	fprintf(thrData.output, ">>> VmSize_end(kB)           %zu\n", vmInfo.VmSize_end);
	fprintf(thrData.output, ">>> VmPeak(kB)               %zu\n", vmInfo.VmPeak);
	fprintf(thrData.output, ">>> VmRSS_start(kB)          %zu\n", vmInfo.VmRSS_start);
	fprintf(thrData.output, ">>> VmRSS_end(kB)            %zu\n", vmInfo.VmRSS_end);
	fprintf(thrData.output, ">>> VmHWM(kB)                %zu\n", vmInfo.VmHWM);
	fprintf(thrData.output, ">>> VmLib(kB)                %zu\n", vmInfo.VmLib);
	fprintf(thrData.output, ">>> num_mprotect             %u\n", num_mprotect.load(relaxed));
	fprintf(thrData.output, ">>> blowup_allocations       %u\n", blowup_allocations.load(relaxed));
	fprintf(thrData.output, ">>> blowup_bytes             %zu\n", blowup_bytes);
	fprintf(thrData.output, ">>> alignment                %zu\n", alignment);
	fprintf(thrData.output, ">>> metadata_object          %zu\n", metadata_object);
	fprintf(thrData.output, ">>> metadata_overhead        %zu\n", metadata_overhead);
	fprintf(thrData.output, ">>> totalSizeAlloc           %zu\n", totalSizeAlloc);
	fprintf(thrData.output, ">>> active_mem_HWM           %zu\n", active_mem_HWM.load(relaxed));
	fprintf(thrData.output, ">>> totalMemOverhead         %zu\n", totalMemOverhead);
	fprintf(thrData.output, ">>> memEfficiency            %.2f%%\n", memEfficiency);
	fprintf(thrData.output, ">>> summation_blowup_bytes         %zu\n", summation_blowup_bytes.load(relaxed));
	fprintf(thrData.output, ">>> summation_blowup_allocations   %u\n", summation_blowup_allocations.load(relaxed));

	writeOverhead();
	writeAddressUsage();
	writeMappings();
	writeThreadMaps();

	fflush (thrData.output);
}

void writeThreadMaps () {

    fprintf (thrData.output, "\n>>>>> TOTALS NOT FROM FREELIST <<<<<\n");

    for (auto const &p : allThreadsTadMap){
        fprintf (thrData.output, "Class Size:       %ld\n", p.first);

        fprintf (thrData.output, "mallocs               %ld\n", p.second->numMallocs);
        fprintf (thrData.output, "reallocs              %ld\n", p.second->numReallocs);
        fprintf (thrData.output, "frees                 %ld\n", p.second->numFrees);

        fprintf (thrData.output, "malloc faults         %ld\n", p.second->numMallocFaults);
        fprintf (thrData.output, "realloc faults        %ld\n", p.second->numReallocFaults);
        fprintf (thrData.output, "free faults           %ld\n", p.second->numFreeFaults);

        fprintf (thrData.output, "malloc tlb misses     %ld\n", p.second->numMallocTlbMisses);
        fprintf (thrData.output, "realloc tlb misses    %ld\n", p.second->numReallocTlbMisses);
        fprintf (thrData.output, "free tlb misses       %ld\n", p.second->numFreeTlbMisses);

        fprintf (thrData.output, "malloc cache misses   %ld\n", p.second->numMallocCacheMisses);
        fprintf (thrData.output, "realloc cache misses  %ld\n", p.second->numReallocCacheMisses);
        fprintf (thrData.output, "free cache misses     %ld\n", p.second->numFreeCacheMisses);

        fprintf (thrData.output, "malloc cache refs     %ld\n", p.second->numMallocCacheRefs);
        fprintf (thrData.output, "realloc cache refs    %ld\n", p.second->numReallocCacheRefs);
        fprintf (thrData.output, "free cache refs       %ld\n", p.second->numFreeCacheRefs);

        fprintf (thrData.output, "num malloc instr      %ld\n", p.second->numMallocInstrs);
        fprintf (thrData.output, "num Realloc instr     %ld\n", p.second->numReallocInstrs);
        fprintf (thrData.output, "num free instr        %ld\n", p.second->numFreeInstrs);
        fprintf(thrData.output, "\n");
    }

    fprintf (thrData.output, "\n>>>>> TOTALS FROM FREELIST <<<<<\n");
    
    for (auto const &p : allThreadsTadMap){
        fprintf (thrData.output, "Class Size:       %ld\n", p.first);

        fprintf (thrData.output, "mallocs               %ld\n", p.second->numMallocsFFL);
        fprintf (thrData.output, "reallocs              %ld\n", p.second->numReallocsFFL);

        fprintf (thrData.output, "malloc faults         %ld\n", p.second->numMallocFaultsFFL);
        fprintf (thrData.output, "realloc faults        %ld\n", p.second->numReallocFaultsFFL);

        fprintf (thrData.output, "malloc tlb misses     %ld\n", p.second->numMallocTlbMissesFFL);
        fprintf (thrData.output, "realloc tlb misses    %ld\n", p.second->numReallocTlbMissesFFL);

        fprintf (thrData.output, "malloc cache misses   %ld\n", p.second->numMallocCacheMissesFFL);
        fprintf (thrData.output, "realloc cache misses  %ld\n", p.second->numReallocCacheMissesFFL);

        fprintf (thrData.output, "malloc cache refs     %ld\n", p.second->numMallocCacheRefsFFL);
        fprintf (thrData.output, "realloc cache refs    %ld\n", p.second->numReallocCacheRefsFFL);

        fprintf (thrData.output, "num malloc instr      %ld\n", p.second->numMallocInstrsFFL);
        fprintf (thrData.output, "num Realloc instr     %ld\n\n", p.second->numReallocInstrsFFL);
    }
}

void writeOverhead () {

	fprintf (thrData.output, "\n-------------metadata-------------\n");
	for (auto o = overhead.begin(); o != overhead.end(); o++) {

		auto key = o.getKey();
		auto data = o.getData();
		fprintf (thrData.output, ">>> classSize %zu\tmetadata %zu\tblowup %zu\talignment %zu\n",
				 key, data->getMetadata(), data->getBlowup(), data->getAlignment());
	}
}

void writeContention () {

	fprintf (thrData.output, "\n------------lock usage------------\n");
	for (auto lock = lockUsage.begin(); lock != lockUsage.end(); lock++) 
		fprintf (thrData.output, "lockAddr= %#lx  maxContention= %d\n",
					lock.getKey(), lock.getData()->maxContention);

}

void writeAddressUsage () {

	fprintf (thrData.output, "\n----------memory usage----------\n");

	for (auto t = addressUsage.begin(); t != addressUsage.end(); t++) {
		auto data = t.getData();
		fprintf (thrData.output, ">>> addr= %#lx numAccesses= %lu szTotal= %zu "
				"szFreed= %zu numAllocs= %lu numFrees= %lu\n",
				data->addr, data->numAccesses, data->szTotal,
				data->szFreed, data->numAllocs, data->numFrees);
	}
}

void writeMappings () {

	int i = 0;
	fprintf (thrData.output, "\n------------mappings------------\n");
	for (auto r = mappings.begin(); r != mappings.end(); r++) {
		auto data = r.getData();
		fprintf (thrData.output,
			"Region[%d]: origin= %c, start= %#lx, end= %#lx, length= %zu, "
			"tid= %d, rw= %lu, allocations= %u\n",
			i, data->origin, data->start, data->end, data->length,
			data->tid, data->rw, data->allocations.load(relaxed));
		i++;
	}
}

//Get the freelist for this objects class size
Freelist* getFreelist (size_t size) {

	getFreelistLock.lock();
	Freelist* f = nullptr;

	if (classSizes->empty()) {
		fprintf (stderr, "ClassSizes is empty. Abort\n");
		abort();
	}

	size_t classSize = getClassSizeFor(size);

	if (freelistMap.find(classSize, &f)) {
		if (d_getFreelist) printf ("Freelist for size %zu found. Returning freelist[%zu]\n", size, classSize);
	}

	else {
		printf ("Didn't find Freelist for size %zu\n", size);
	}

	if (f == nullptr) {
		printf ("Can't find freelist, aborting\n");
		abort();
	}

	getFreelistLock.unlock();
	return f;
}

void calculateMemOverhead () {

	for (auto o = overhead.begin(); o != overhead.end(); o++) {
		
		auto data = o.getData();
		alignment += data->getAlignment();
		blowup_bytes += data->getBlowup();
	}

	totalMemOverhead += alignment;
	totalMemOverhead += blowup_bytes;

	for (auto t = addressUsage.begin(); t != addressUsage.end(); t++) {
		auto data = t.getData();
		totalSizeAlloc += data->szTotal;
		totalSizeFree += data->szFreed;
		totalSizeDiff = totalSizeAlloc - totalSizeFree;
	}
	
	//Calculate metadata_overhead by using the per-object value
	long allocations = (num_malloc.load(relaxed) + num_calloc.load(relaxed) + num_realloc.load(relaxed));
	metadata_overhead = (allocations * metadata_object);
	
	totalMemOverhead += metadata_overhead;
	memEfficiency = ((float) totalSizeAlloc / (totalSizeAlloc + totalMemOverhead)) * 100;
}

void get_bp_metadata() {
	size_t size1 = 128;
	size_t size2 = 128;
	size_t metadata = 0;

	void* ptr1 = RealX::malloc(size1);
	if (d_metadata) printf ("ptr1= %p\n", ptr1);
	void* ptr2 = RealX::malloc(size2);
	if (d_metadata) printf ("ptr2= %p\n", ptr2);
	void* moved;
	
	long first = (long) ptr1;
	long second = (long) ptr2;
	long m = 0;

	long diff = second - first;
	metadata = diff - 128;

	//Once object moves, diff will be negative
	while (diff > 0) {
		size1++;
		moved = RealX::realloc(ptr1, size1);
		m = (long) moved;
		if ((diff = second - m) < 0) break;
		if (d_metadata) {
			printf ("size1 = %zu\n", size1);
			printf ("diff = %lu\n", diff);
		}
		metadata--;
	}

	if (d_metadata) printf ("metadata = %zu\n", metadata);

	RealX::free(ptr2);
	metadata_object = metadata;
}

void get_bibop_metadata() {
	
	if (d_bibop_metadata) printf ("in get_bibop_metadata\n");

	bool done = false;
	int bytes_index = 0;
	int bytes_max = 0;
	uintptr_t vpage = 0;
	unsigned add_pages = 0;
	unsigned bytes_in_use[100];

	for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
		if (done == true) break;
		auto data = entry.getData();
		char origin = data->origin;
		if (data->allocations > 0) {
			//Don't look for metadata here
			if (d_bibop_metadata) {}
		}
		//This mmap wasn't used for satisfying allocations
		//Check for metadata
		else if ((origin == 'a') || (origin == 's')) {
			if (d_bibop_metadata) printf ("found unused mmap, start= %#lx, finding page..\n", data->start);

			add_pages = find_page (data->start, data->end);

			if (d_bibop_metadata) printf ("add_pages returned %d\n", add_pages);

			vpage = (data->start + (add_pages*4096));

			if (d_bibop_metadata) printf ("vpage= %#lx\n", vpage);

			unsigned bytes = search_vpage(vpage);
			if (bytes > 0) {
				//Keep track of values return from search_vpage function
				//Store in array to get the lowest value later
				bytes_in_use[bytes_index] = bytes;
				bytes_index++;
			}
		}
	}
	
	bytes_max = bytes_index;
	unsigned smallest = bytes_in_use[0];
	if (d_bibop_metadata) printf ("bytes_in_use[0] = %u\n", bytes_in_use[0]);
	for (int i = 1; i < bytes_max; i++) {
		if (d_bibop_metadata) printf ("bytes_in_use[%d] = %u\n", i, bytes_in_use[i]);
		if (bytes_in_use[i] < smallest) smallest = bytes_in_use[i];
	}

	if (d_bibop_metadata) printf ("Finished metadata. Returning\n");
	metadata_object = smallest;
}

/*
Read in 8 bytes at a time on this vpage and compare it
with 0. Looking for "used" bytes. If the value is anything
other than 0, consider it used.
*/
unsigned search_vpage (uintptr_t vpage) {
	
	unsigned bytes = 0;
	uint64_t zero = 0;
	uint64_t word;
	for (unsigned offset = 0; offset <= 4088; offset += 8) {

		word = *((uint64_t*) (vpage + offset));
		if ((word | zero) > 0) {
			bytes += 8;
		}
	}
	return bytes;
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

//Read 8 byte increments from the pagemap file looking for
//vpages that are physically backed. "add_pages" will count how
//many pages it searched before it found a physically backed
//page. Then we can take that value and add it to the starting
//mmap address, then we have the address of the first vpage
//in this mmap region with a physical page backing
int find_page(uintptr_t vstart, uintptr_t vend) {
	char pagemap_filename[50];
	snprintf (pagemap_filename, 50, "/proc/%d/pagemap", pid);
	int fdmap;
	uint64_t bitmap;
	unsigned long pagenum_start, pagenum_end;
	uint64_t num_pages_read = 0;
	uint64_t num_pages_to_read = 0;
	unsigned add_pages = 0;

	if((fdmap = open(pagemap_filename, O_RDONLY)) == -1) {
		perror ("failed to open pagemap_filename");
		return -1;
	}

	pagenum_start = vstart >> PAGE_BITS;
	pagenum_end = vend >> PAGE_BITS;
	num_pages_to_read = pagenum_end - pagenum_start + 1;
	if(num_pages_to_read == 0) {
		fprintf (stderr, "num_pages_to_read == 0\n");
		close(fdmap);
		return -1;
	}

	if(lseek(fdmap, (pagenum_start * ENTRY_SIZE), SEEK_SET) == -1) {
		perror ("failed to lseek on file");
		close(fdmap);
		return -1;
	}

	do {
		if(read(fdmap, &bitmap, ENTRY_SIZE) != ENTRY_SIZE) {
			perror ("failed to read 8 bytes");
			close(fdmap);
			return -1;
		}

		num_pages_read++;
		if((bitmap >> 63) == 1) {
			break;
		}
		add_pages++;
	} while(num_pages_read < num_pages_to_read);

	close(fdmap);
	return add_pages;
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

void clearFreelists() {
	
	uint64_t keys [10];

	for (auto freelist = freelistMap.begin(); freelist != freelistMap.end(); freelist++) {
		int i = 0;
		auto f = *(freelist.getData());
		for (auto entry = f.begin(); entry != f.end(); entry++) {
			auto key = entry.getKey();
			keys[i] = key;
			i++;
		}
		int last = i;
		for (int j = 0; j < last; j++) {
			f.erase(keys[j]);
		}
	}
}

bool mappingEditor (void* addr, size_t len, int prot) {
	
	bool found = false;
	for (auto mapping = mappings.begin(); mapping != mappings.end(); mapping++) {
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

void doBefore (PerfReadInfo* before, uint64_t* tsc_before) {
	
	getPerfInfo(before);
	*tsc_before = rdtscp();
}

void doAfter (PerfReadInfo* after, uint64_t* tsc_after) {
	
	getPerfInfo(after);
	*tsc_after = rdtscp();
}

void analyzePerfInfo (PerfReadInfo* before, PerfReadInfo* after, size_t classSize, bool* reused, pid_t tid) {

	bool reuseFreeObject = *reused;
	size_t currentClassSize = classSize;
	classSizeMap *csm;
	thread_alloc_data *tad;
        
	if(bibop){   
		if (!tadMap.find(tid, &csm)){
			csm = (classSizeMap*) myMalloc(sizeof(classSizeMap));
			csm->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
			for (auto cSize = classSizes->begin (); cSize != classSizes->end (); cSize++){
				csm->insertIfAbsent(*cSize, newTad());
				allThreadsTadMap[*cSize] = newTad();
			}
			csm->insertIfAbsent(0, newTad());
			allThreadsTadMap[0] = newTad();
			tadMap.insertIfAbsent(tid, csm);
		}
	}

	else {
		if (!tadMap.find(tid, &csm)){
			csm = (classSizeMap*) myMalloc(sizeof(classSizeMap));
			csm->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
		}
		csm->insertIfAbsent(0, newTad());
		allThreadsTadMap[0] = newTad();
		tadMap.insertIfAbsent(tid, csm);
	}

	csm = nullptr;
	if(tadMap.find(tid, &csm)){
		if(d_pmuData){
			fprintf(stderr, "current class size: %lu\n", currentClassSize);
			fprintf(stderr, "found tid -> csm\n");
		}

		if(csm->find(currentClassSize, &tad)){
			if(d_pmuData)
				fprintf(stderr, "found csm -> tad\n");

			if (!reuseFreeObject){
				tad->numMallocs += 1;
				tad->numMallocFaults += after->faults - before->faults;
				tad->numMallocTlbMisses += after->tlb_misses - before->tlb_misses;
				tad->numMallocCacheMisses += after->cache_misses - before->cache_misses;
				tad->numMallocCacheRefs += after->cache_refs - before->cache_refs;
				tad->numMallocInstrs += after->instructions - before->instructions;
			}
			else{
				tad->numMallocsFFL += 1;
				tad->numMallocFaultsFFL += after->faults - before->faults;
				tad->numMallocTlbMissesFFL += after->tlb_misses - before->tlb_misses;
				tad->numMallocCacheMissesFFL += after->cache_misses - before->cache_misses;
				tad->numMallocCacheRefsFFL += after->cache_refs - before->cache_refs;
				tad->numMallocInstrsFFL += after->instructions - before->instructions;
			}
		}
	}

	if(after->instructions - before->instructions != 0 && d_pmuData){
		fprintf(stderr, "Malloc from thread %d\n"
						"From free list:    %s\n"
						"Num faults:        %ld\n"
						"Num TLB misses:    %ld\n"
						"Num cache misses:  %ld\n"
						"Num cache refs:    %ld\n"
						"Num instructions:  %ld\n\n", 
				tid, reuseFreeObject ? "true" : "false",
				after->faults - before->faults, after->tlb_misses - before->tlb_misses,
				after->cache_misses - before->cache_misses, after->cache_refs - before->cache_refs,
				after->instructions - before->instructions);
	}
}
