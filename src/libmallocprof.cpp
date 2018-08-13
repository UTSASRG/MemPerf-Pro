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

#define CALLSITE_MAXIMUM_LENGTH 20
#define ENTRY_SIZE 8
#define PAGE_BITS 12 

__thread thread_data thrData;

extern char data_start;
extern char _etext;
extern char * program_invocation_name;
extern void * __libc_stack_end;
extern char __executable_start;

const char *CLASS_SIZE_FILE_NAME = "class_sizes.txt";
FILE *classSizeFile;
char *allocator_name;

enum initStatus{
    INIT_ERROR = -1,
    NOT_INITIALIZED = 0, 
    IN_PROGRESS = 1, 
    INITIALIZED = 2
};

initStatus mallocInitialized = NOT_INITIALIZED;

//Globals
uint64_t min_pos_callsite_id;
size_t malloc_mmap_threshold = 0;
pid_t pid;

bool bumpPointer = false;
bool bibop = false;
bool inGetMmapThreshold = false;
bool usingRealloc = false;
bool mapsInitialized = false;
bool mmap_found = false;
bool gettingClassSizes = false;
bool inRealMain = false;
bool firstMalloc = false;
bool selfmapInitialized = false;
size_t totalSizeAlloc = 0;
size_t totalSizeFree = 0;
size_t totalSizeDiff = 0;
size_t totalMemOverhead = 0;
uint64_t unused_mmap_region_size = 0;
uint64_t unused_mmap_region_count = 0;
uint64_t metadata_used_pages = 0;
size_t metadata_object = 0;
size_t metadata_overhead = 0;
float memEfficiency = 0;

//Debugging flags
const bool debug = false;
const bool d_malloc_info = false;
const bool d_mprotect = false;
const bool d_mmap = false;
const bool d_metadata = false;
const bool d_bibop_metadata = true;
const bool d_style = true;

//Atomic Globals
std::atomic_uint allThreadsNumFaults (0);
std::atomic_uint allThreadsNumTlbMisses (0);
std::atomic_uint allThreadsNumCacheMisses (0);
std::atomic_uint allThreadsNumCacheRefs (0);
std::atomic_uint allThreadsNumInstrs (0);
std::atomic_uint num_free (0);
std::atomic_uint num_malloc (0);
std::atomic_uint num_calloc (0);
std::atomic_uint num_realloc (0);
std::atomic_uint new_address (0);
std::atomic_uint reused_address (0);
std::atomic_uint num_pthread_mutex_locks (0);
std::atomic_uint num_trylock (0);
std::atomic_uint total_waits (0);
std::atomic<std::uint64_t> total_time_wait (0);
std::atomic_uint dontneed_advice_count (0);
std::atomic_uint madvise_calls (0);
std::atomic_uint numSbrks (0);
std::atomic_uint programBreakChange (0);
std::atomic_uint malloc_mmaps (0);
std::atomic_uint total_mmaps (0);
std::atomic_uint temp_alloc (0);
std::atomic_uint temp_position (0);
std::atomic<std::uint64_t> cycles_free (0);
std::atomic<std::uint64_t> cycles_new (0);
std::atomic<std::uint64_t> cycles_reused (0);
std::atomic_uint threads (0);
std::atomic_size_t blowup_bytes (0);
std::atomic_uint blowup_allocations (0);
std::atomic_size_t summation_blowup_bytes (0);
std::atomic_uint summation_blowup_allocations (0);
std::atomic_size_t fragmentation (0);
std::atomic_size_t free_bytes (0);
std::atomic_size_t active_mem (0);
std::atomic_size_t active_mem_HWM (0);
std::atomic_uint num_mprotect (0);

//Vector of class sizes
std::vector <uint64_t> classSizes;
std::vector<uint64_t>::iterator csBegin, csEnd;
size_t largestClassSize;

//Struct of virtual memory info
VmInfo vmInfo;

#define relaxed std::memory_order_relaxed
#define acquire std::memory_order_acquire
#define release std::memory_order_release
#define TEMP_MEM_SIZE 65536000
#define LARGE_OBJECT 512

//Thread local variables
thread_local uint64_t numFaults;
thread_local uint64_t numTlbMisses;
thread_local uint64_t numCacheMisses;
thread_local uint64_t numCacheRefs;
thread_local uint64_t numInstrs;
thread_local uint64_t timeAttempted;
thread_local uint64_t timeWaiting;
thread_local uint64_t numWaits;
thread_local bool waiting;
thread_local bool inAllocation;
thread_local bool inMmap;
thread_local bool samplingInit = false;

//Stores info about locks
typedef struct LockContention {
	int contention;
	int maxContention;
} LC;

struct stack_frame {
	struct stack_frame * prev;	// pointing to previous stack_frame
	void * caller_address;		// the address of caller
};

//Hashmap of thread IDs to their thread_alloc_data structs
HashMap <uint64_t, thread_alloc_data*, spinlock> tadMap;

//Hashmap of malloc'd addresses to a ObjectTuple
HashMap <uint64_t, ObjectTuple*, spinlock> addressUsage;

//Hashmap of lock addr to LC
HashMap <uint64_t, LC*, spinlock> lockUsage;

//Hashmap of mmap addrs to tuple:
HashMap <uint64_t, MmapTuple*, spinlock> mappings;

//Hashmap of freelists, key 0 is bump pointer
typedef HashMap <uint64_t, FreeObject*, spinlock> Freelist;
HashMap <uint64_t, Freelist*, spinlock> freelistMap;

//Spinlocks
spinlock temp_mem_lock;
spinlock getFreelistLock;
spinlock freelistLock;
spinlock mappingsLock;

// Functions 
void getAllocStyle ();
void getClassSizes ();
void getMmapThreshold ();
void writeAllocData ();
void writeContention ();
void* myMalloc (size_t size);
void myFree (void* ptr);
ObjectTuple* newObjectTuple (uint64_t address, size_t size);
MmapTuple* newMmapTuple (uint64_t address, size_t length, int prot);
LC* newLC ();
thread_alloc_data* newTad();
void getMemUsageStart ();
void getMemUsageEnd ();
void writeAddressUsage ();
void globalizeThreadAllocData();
void test();
void test4();
FreeObject* newFreeObject (uint64_t addr, uint64_t size);
Freelist* getFreelist (uint64_t size);
void calculateMemOverhead ();
void writeMappings();
int num_used_pages(uintptr_t vstart, uintptr_t vend);
int find_page(uintptr_t vstart, uintptr_t vend);
void get_bp_metadata();
void get_bibop_metadata();
void clearFreelists();
void resetAtomics();
unsigned search_vpage (uintptr_t vpage);

// pre-init private allocator memory
char tmpbuf[TEMP_MEM_SIZE];

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
	addressUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	lockUsage.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	mappings.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
	freelistMap.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);

	mapsInitialized = true;
    allocator_name = (char *) myMalloc(100 * sizeof(char));

	void * program_break = sbrk(0);
	thrData.stackStart = (char *)__builtin_frame_address(0);
	thrData.stackEnd = (char *)__libc_stack_end;

	mallocInitialized = INITIALIZED;

	//These have to be done after mallocInitialized = INITIALIZED
	//This function opens the /proc/self/maps file using ifstream
	//constructor which calls operator new, which calls malloc
	//Which then triggers the allocators initializer, which then
	//mmaps memory. We need to be initialized so we can record mmaps
	//This wouldn't be a problem if we could intercept that mallocs
	//However, some allocators (OBSD, DieHarder) intercept
	//operator new, and directly call their malloc functions
	//eg. obsd_malloc, xxmalloc.
	selfmap::getInstance().getTextRegions();
	selfmapInitialized = true;
	
	// Determines allocator style: bump pointer or bibop
	getAllocStyle ();

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
		getClassSizes ();
	}
	else {
		getMmapThreshold();
		Freelist* small = (Freelist*) myMalloc (sizeof (Freelist));
		small->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
		freelistMap.insert(0, small);
		if (debug) printf ("Inserting freelistMap[0]\n");
		Freelist* large = (Freelist*) myMalloc (sizeof (Freelist));
		large->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
		freelistMap.insert(LARGE_OBJECT, large);
		get_bp_metadata();
	}

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
		
		if (!mapsInitialized) return RealX::malloc (sz);

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

        int64_t before_faults, before_tlb_misses, before_cache_misses, before_cache_refs, before_instrs;
        int64_t after_faults, after_tlb_misses, after_cache_misses, after_cache_refs, after_instrs;
        uint64_t tid = gettid();

		//thread_local
		inAllocation = true;

		//Collect allocation data
		void* objAlloc;

        getPerfInfo(&before_faults, &before_tlb_misses, &before_cache_misses,
                &before_cache_refs, &before_instrs);
		uint64_t before = rdtscp ();
		objAlloc = RealX::malloc(sz);
		uint64_t after = rdtscp ();
        getPerfInfo(&after_faults, &after_tlb_misses, &after_cache_misses, &after_cache_refs, &after_instrs);
        
        uint64_t cyclesForMalloc = after - before;
		uint64_t address = reinterpret_cast <uint64_t> (objAlloc);

		//Check for Mem Blowup
		if (sz <= malloc_mmap_threshold) {

			//Check for internal fragmentation
			if (bibop) {

				for (auto s = csBegin; s != csEnd; s++) {

					auto classSize = *s;
					if (sz > classSize) continue;
					else if (sz <= classSize) {
						//Fragmentation = classSize - sz
						int fragBytes = classSize - sz;
						fragmentation.fetch_add (fragBytes, relaxed);
						break;
					}
				}
			}//End fragmentation check

			freelistLock.lock();
			Freelist* freelist;
			bool reuseFreePossible = false;
			bool reuseFreeObject = false;
		
			if (!bibop) {
				if (sz < LARGE_OBJECT) {

					if (freelistMap.find(0, &freelist)) {
						if (debug) printf ("FreelistMap[0] found\n");
						for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
							auto data = f.getData();
							if (sz <= data->size) {
								reuseFreePossible = true;
								if (debug) printf ("reuseFreePossible= true, data->size= %zu\n", data->size);
							}
							if (address == data->addr) {
								reuseFreeObject = true;
								(*freelist).erase(address);
								free_bytes -= sz;
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
						if (debug) printf ("FreelistMap[LARGE_OBJECT] found\n");
						for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
							auto data = f.getData();
							if (sz <= data->size) {
								reuseFreePossible = true;
								if (debug) printf ("reuseFreePossible= true, data->size= %zu\n", data->size);
							}
							if (address == data->addr) {
								reuseFreeObject = true;
								(*freelist).erase(address);
								free_bytes -= sz;
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
				freelist = getFreelist (sz);
				if ((*freelist).begin() != (*freelist).end()) {
					reuseFreePossible = true;
					for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
						auto data = f.getData();
						if (address == data->addr) {
							reuseFreeObject = true;
							(*freelist).erase(address);
							free_bytes -= sz;
							break;
						}
					}
				}
			}

			//If Reuse was possible, but didn't reuse
			if (reuseFreePossible && !reuseFreeObject) {
				//possible memory blowup has occurred
				//get size of allocation
				blowup_bytes.fetch_add(sz, relaxed);
				//increment counter
				blowup_allocations.fetch_add(1, relaxed);
			}
			freelistLock.unlock();
		}//End Check for Mem Blowup

		ObjectTuple* t;
		//Has this address been used before
		if (addressUsage.find (address, &t)) {
			t->numAccesses++;
			t->szTotal += sz;
			t->numAllocs++;
			t->szUsed = sz;
			reused_address.fetch_add (1, relaxed);
			cycles_reused.fetch_add (cyclesForMalloc, relaxed);
		}

		else {
			new_address.fetch_add (1, relaxed);
			cycles_new.fetch_add (cyclesForMalloc, relaxed);
			addressUsage.insertIfAbsent (address, newObjectTuple(address, sz));
		}

        tadMap.insertIfAbsent(tid, newTad()); 
        thread_alloc_data *tad;
        
        if (tadMap.find(tid, &tad)){
            
            tad->numFaults += after_faults - before_faults;
            tad->numTlbMisses += after_tlb_misses - before_tlb_misses;
            tad->numCacheMisses += after_cache_misses - before_cache_misses;
            tad->numCacheRefs += after_cache_refs - before_cache_refs;
            tad->numInstrs += after_instrs - before_instrs; 

        }

		mappingsLock.lock();
		for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
			auto data = entry.getData();
			if (data->start <= address && address <= data->end) {
				if (debug) printf ("allocating from mmapped region\n");
				data->allocations.fetch_add(1, relaxed);
				break;
			}
		}
		mappingsLock.unlock();

		if (sz <= free_bytes.load()) {
			summation_blowup_bytes.fetch_add(sz, relaxed);
			summation_blowup_allocations.fetch_add(1, relaxed);
		}

		uint32_t n = active_mem += sz;
		if ((n + sz) > active_mem_HWM) {
			active_mem_HWM = (n + sz);
			if (d_malloc_info) printf ("increasing a_m_HWM, n= %u, sz= %zu\n", n, sz);
		}

		if ((num_malloc.fetch_add(1, relaxed)) == 0) {
		}

		//thread_local
		inAllocation = false;

		return objAlloc;
	}

	void * yycalloc(size_t nelem, size_t elsize) {

		//If !mapsInitialized, then malloc hasn't initialized
		//Will get its memory from myMalloc
		if(!mapsInitialized) {

			void * ptr = NULL;
			ptr = yymalloc (nelem * elsize);
			if (ptr) memset(ptr, 0, nelem * elsize);
			return ptr;
		}

		else if (inAllocation) {
			return RealX::calloc(nelem, elsize);
		}

        if(!samplingInit){
            initSampling();
            samplingInit = true;
        }

		if (!inRealMain) return RealX::calloc (nelem, elsize);

        int64_t before_faults, before_tlb_misses, before_cache_misses, before_cache_refs, before_instrs;
        int64_t after_faults, after_tlb_misses, after_cache_misses, after_cache_refs, after_instrs;
        uint64_t tid = gettid();
		
        //thread_local
		inAllocation = true;

		void *objAlloc;

        getPerfInfo(&before_faults, &before_tlb_misses, &before_cache_misses,
                &before_cache_refs, &before_instrs);
		uint64_t before = rdtscp();
		objAlloc = RealX::calloc(nelem, elsize);
		uint64_t after = rdtscp();
        getPerfInfo(&after_faults, &after_tlb_misses, &after_cache_misses, &after_cache_refs, &after_instrs);
	
        uint64_t cyclesForCalloc = after - before;
		uint64_t address = reinterpret_cast <uint64_t> (objAlloc);

		size_t sz = nelem*elsize;

		//Insert Here
		//Check for Mem Blowup
		if (sz <= malloc_mmap_threshold) {

			//Check for internal fragmentation
			if (bibop) {

				for (auto s = csBegin; s != csEnd; s++) {

					auto classSize = *s;
					if (sz > classSize) continue;
					else if (sz <= classSize) {
						//Fragmentation = classSize - sz
						int fragBytes = classSize - sz;
						fragmentation.fetch_add (fragBytes, relaxed);
						break;
					}
				}
			}//End fragmentation check

			freelistLock.lock();
			Freelist* freelist;
			bool reuseFreePossible = false;
			bool reuseFreeObject = false;
		
			if (!bibop) {
				if (sz < LARGE_OBJECT) {

					if (freelistMap.find(0, &freelist)) {
						if (debug) printf ("FreelistMap[0] found\n");
						for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
							auto data = f.getData();
							if (sz <= data->size) {
								reuseFreePossible = true;
								if (debug) printf ("reuseFreePossible= true, data->size= %zu\n", data->size);
							}
							if (address == data->addr) {
								reuseFreeObject = true;
								(*freelist).erase(address);
								free_bytes -= sz;
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
						if (debug) printf ("FreelistMap[LARGE_OBJECT] found\n");
						for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
							auto data = f.getData();
							if (sz <= data->size) {
								reuseFreePossible = true;
								if (debug) printf ("reuseFreePossible= true, data->size= %zu\n", data->size);
							}
							if (address == data->addr) {
								reuseFreeObject = true;
								(*freelist).erase(address);
								free_bytes -= sz;
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
				freelist = getFreelist (sz);
				if ((*freelist).begin() != (*freelist).end()) {
					reuseFreePossible = true;
					for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
						auto data = f.getData();
						if (address == data->addr) {
							reuseFreeObject = true;
							(*freelist).erase(address);
							free_bytes -= sz;
							break;
						}
					}
				}
			}

			//If Reuse was possible, but didn't reuse
			if (reuseFreePossible && !reuseFreeObject) {
				//possible memory blowup has occurred
				//get size of allocation
				blowup_bytes.fetch_add(sz, relaxed);
				//increment counter
				blowup_allocations.fetch_add(1, relaxed);
			}
			freelistLock.unlock();
		}//End Check for Mem Blowup

		ObjectTuple* t;
		if ( addressUsage.find(address, &t) ){
			t->numAccesses++;
			t->szTotal += (sz);
			t->szUsed = (sz);
			t->numAllocs++;
			reused_address.fetch_add(1, relaxed);
			cycles_reused.fetch_add(cyclesForCalloc, relaxed);
      }
      
		else{
			new_address.fetch_add (1, relaxed);
			cycles_new.fetch_add (cyclesForCalloc, relaxed);
			addressUsage.insertIfAbsent(address, newObjectTuple(address, (sz)));
      }

		num_calloc.fetch_add (1, relaxed);

        tadMap.insertIfAbsent(tid, newTad()); 
        thread_alloc_data *tad;
        
        if (tadMap.find(tid, &tad)){
            
            tad->numFaults += after_faults - before_faults;
            tad->numTlbMisses += after_tlb_misses - before_tlb_misses;
            tad->numCacheMisses += after_cache_misses - before_cache_misses;
            tad->numCacheRefs += after_cache_refs - before_cache_refs;
            tad->numInstrs += after_instrs - before_instrs; 

        }

		mappingsLock.lock();
		for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
			auto data = entry.getData();
			if (data->start <= address && address <= data->end) {
				if (debug) printf ("allocating from mmapped region\n");
				data->allocations.fetch_add(1, relaxed);
				break;
			}
		}
		mappingsLock.unlock();

		if (sz <= free_bytes.load()) {
			summation_blowup_bytes.fetch_add(sz, relaxed);
			summation_blowup_allocations.fetch_add(1, relaxed);
		}

		//thread_local
		inAllocation = false;

		return objAlloc;
	}

	void yyfree(void * ptr) {
		if(ptr == NULL)
			return;

        if(!samplingInit){
            initSampling();
            samplingInit = true;
        }

        int64_t before_faults, before_tlb_misses, before_cache_misses, before_cache_refs, before_instrs;
        int64_t after_faults, after_tlb_misses, after_cache_misses, after_cache_refs, after_instrs;
        uint64_t tid = gettid();
		uint64_t size = 0;

		// Determine whether the specified object came from our global buffer;
		// only call RealX::free() if the object did not come from here.
		if((ptr >= (void *) tmpbuf) && (ptr <= (void *)(tmpbuf + TEMP_MEM_SIZE))) 
				myFree (ptr);

		else if (!inRealMain) {
			return RealX::free (ptr);
		}
		else {

			uint64_t address = reinterpret_cast <uint64_t> (ptr);
			ObjectTuple* t;

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

            getPerfInfo(&before_faults, &before_tlb_misses, &before_cache_misses,
                    &before_cache_refs, &before_instrs);

			uint64_t before = rdtscp ();
			RealX::free(ptr);
			uint64_t after = rdtscp ();
           
            getPerfInfo(&after_faults, &after_tlb_misses, &after_cache_misses,
                    &after_cache_refs, &after_instrs);
			num_free.fetch_add(1, relaxed);
			cycles_free.fetch_add ((after - before), relaxed);

            tadMap.insertIfAbsent(tid, newTad()); 
            thread_alloc_data *tad;
            
            if (tadMap.find(tid, &tad)){
                
                tad->numFaults += after_faults - before_faults;
                tad->numTlbMisses += after_tlb_misses - before_tlb_misses;
                tad->numCacheMisses += after_cache_misses - before_cache_misses;
                tad->numCacheRefs += after_cache_refs - before_cache_refs;
                tad->numInstrs += after_instrs - before_instrs; 

            }
		}
		
		free_bytes += size;
		active_mem -= size;
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

        int64_t before_faults, before_tlb_misses, before_cache_misses, before_cache_refs, before_instrs;
        int64_t after_faults, after_tlb_misses, after_cache_misses, after_cache_refs, after_instrs;
        uint64_t tid = gettid();

		//thread_local
		inAllocation = true;

		void *objAlloc;

        getPerfInfo(&before_faults, &before_tlb_misses, &before_cache_misses,
                &before_cache_refs, &before_instrs);
		uint64_t before = rdtscp();
		objAlloc = RealX::realloc(ptr, sz);
		uint64_t after = rdtscp();
        getPerfInfo(&after_faults, &after_tlb_misses, &after_cache_misses, &after_cache_refs, &after_instrs);

        uint64_t cyclesForRealloc = after - before;
		uint64_t address = reinterpret_cast <uint64_t> (objAlloc);

		//Insert Here
		//Check for Mem Blowup
		if (sz <= malloc_mmap_threshold) {

			//Check for internal fragmentation
			if (bibop) {

				for (auto s = csBegin; s != csEnd; s++) {

					auto classSize = *s;
					if (sz > classSize) continue;
					else if (sz <= classSize) {
						//Fragmentation = classSize - sz
						int fragBytes = classSize - sz;
						fragmentation.fetch_add (fragBytes, relaxed);
						break;
					}
				}
			}//End fragmentation check

			freelistLock.lock();
			Freelist* freelist;
			bool reuseFreePossible = false;
			bool reuseFreeObject = false;
		
			if (!bibop) {
				if (sz < LARGE_OBJECT) {

					if (freelistMap.find(0, &freelist)) {
						if (debug) printf ("FreelistMap[0] found\n");
						for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
							auto data = f.getData();
							if (sz <= data->size) {
								reuseFreePossible = true;
								if (debug) printf ("reuseFreePossible= true, data->size= %zu\n", data->size);
							}
							if (address == data->addr) {
								reuseFreeObject = true;
								(*freelist).erase(address);
								free_bytes -= sz;
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
						if (debug) printf ("FreelistMap[LARGE_OBJECT] found\n");
						for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
							auto data = f.getData();
							if (sz <= data->size) {
								reuseFreePossible = true;
								if (debug) printf ("reuseFreePossible= true, data->size= %zu\n", data->size);
							}
							if (address == data->addr) {
								reuseFreeObject = true;
								(*freelist).erase(address);
								free_bytes -= sz;
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
				freelist = getFreelist (sz);
				if ((*freelist).begin() != (*freelist).end()) {
					reuseFreePossible = true;
					for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
						auto data = f.getData();
						if (address == data->addr) {
							reuseFreeObject = true;
							(*freelist).erase(address);
							free_bytes -= sz;
							break;
						}
					}
				}
			}

			//If Reuse was possible, but didn't reuse
			if (reuseFreePossible && !reuseFreeObject) {
				//possible memory blowup has occurred
				//get size of allocation
				blowup_bytes.fetch_add(sz, relaxed);
				//increment counter
				blowup_allocations.fetch_add(1, relaxed);
			}
			freelistLock.unlock();
		}//End Check for Mem Blowup

		ObjectTuple* t;
		if ( addressUsage.find(address, &t) ){
			t->numAccesses++;
			t->szTotal += sz;
			t->szUsed = sz;
			t->numAllocs++;
			reused_address.fetch_add(1, relaxed);
			cycles_reused.fetch_add (cyclesForRealloc, relaxed);
		}
		
		else{
			new_address.fetch_add(1, relaxed);
			cycles_new.fetch_add (cyclesForRealloc, relaxed);
			addressUsage.insertIfAbsent(address, newObjectTuple(address, sz));
      }

		num_realloc.fetch_add(1, relaxed);
        
        tadMap.insertIfAbsent(tid, newTad()); 
        thread_alloc_data *tad;
        
        if (tadMap.find(tid, &tad)){
            
            tad->numFaults += after_faults - before_faults;
            tad->numTlbMisses += after_tlb_misses - before_tlb_misses;
            tad->numCacheMisses += after_cache_misses - before_cache_misses;
            tad->numCacheRefs += after_cache_refs - before_cache_refs;
            tad->numInstrs += after_instrs - before_instrs; 

        }

		mappingsLock.lock();
		for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
			auto data = entry.getData();
			if (data->start <= address && address <= data->end) {
				if (debug) printf ("allocating from mmapped region\n");
				data->allocations.fetch_add(1, relaxed);
				break;
			}
		}
		mappingsLock.unlock();

		if (sz <= free_bytes.load()) {
			summation_blowup_bytes.fetch_add(sz, relaxed);
			summation_blowup_allocations.fetch_add(1, relaxed);
		}

		//thread_local
		inAllocation = false;

		return objAlloc;
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
			dontneed_advice_count.fetch_add(1, relaxed);

		madvise_calls.fetch_add(1, relaxed);
		return RealX::madvise(addr, length, advice);
	}

    void *sbrk(intptr_t increment){
        if(mallocInitialized != INITIALIZED) return RealX::sbrk(increment);

        void *retptr = RealX::sbrk(increment);
        uint64_t newProgramBreak = (uint64_t) RealX::sbrk(0);
        uint64_t oldProgramBreak = (uint64_t) retptr;
        uint64_t sizeChange = newProgramBreak - oldProgramBreak;

        programBreakChange.fetch_add(sizeChange, relaxed);
        numSbrks.fetch_add(1, relaxed);

        return retptr;
    }
}

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
    tad->numFaults = 0;
    tad->numTlbMisses = 0;
    tad->numCacheMisses = 0;
    tad->numCacheRefs = 0;
    tad->numInstrs = 0;
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
		retptr = (void *)(tmpbuf + temp_position);
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
	if ((ptr >= tmpbuf) && (ptr <= (tmpbuf + TEMP_MEM_SIZE))) {
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

	void* add1 = RealX::malloc (128);

	//Check for mmap'd metadata
	if (total_mmaps > 0) {
		if (d_style) printf ("first malloc resulting in mmap, searching for metadata\n");
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

	if ((address1Page - address2Page) != 0) {

		bibop = true;
//		fprintf (thrData.output, ">>> allocator     bibop\n");
	}

	else {

		bumpPointer = true;
//		fprintf (thrData.output, ">>> allocator     bump-pointer\n");
	}
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

		if(strcmp(line, allocator_name) == 0){
         matchFound = true;
      }
                
      if(matchFound) break;
   }
        
   if(matchFound){
		if (debug) printf ("match found\n");
		getline(&line, &bytesToRead, classSizeFile);
      line[strcspn(line, "\n")] = 0;
                
      while( (token = strsep(&line, " ")) ){
                    
         if(atoi(token) != 0){                        
            classSizes.push_back( (uint64_t) atoi(token));
         }
      }

		auto cSize = classSizes.end();
		cSize--;
		malloc_mmap_threshold = *cSize;
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
				classSizes.push_back (oldSize);
			}

			oldAddr = newAddr;
			oldAddress = newAddress;
			oldSize = newSize;
		}

		RealX::free (newAddress);

		fprintf(classSizeFile, "%s\n", allocator_name);

		for (auto cSize = classSizes.begin (); cSize != classSizes.end (); cSize++) 
			fprintf (classSizeFile, "%zu ", *cSize);

		fprintf(classSizeFile, "\n");
		fflush (classSizeFile);
   }

	fprintf (thrData.output, ">>> classSizes    ");

	if (!classSizes.empty()) {

		for (auto cSize = classSizes.begin(); cSize != classSizes.end(); cSize++) 
			fprintf (thrData.output, "%zu ", *cSize);
	}

	fclose(classSizeFile);
	fprintf (thrData.output, "\n");
	fflush (thrData.output);

	csBegin = classSizes.begin();
	csEnd = classSizes.end();
	largestClassSize = *(csEnd--);
	csEnd++;

	for (auto cSize = csBegin; cSize != csEnd; cSize++) {
		auto classSize = *cSize;
		Freelist* f = (Freelist*) myMalloc (sizeof (Freelist));
		f->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
		freelistMap.insert(classSize, f);
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
    for(auto it = tadMap.begin(); it != tadMap.end(); it++){
        allThreadsNumFaults.fetch_add(it.getData()->numFaults);
        allThreadsNumTlbMisses.fetch_add(it.getData()->numTlbMisses);
        allThreadsNumCacheMisses.fetch_add(it.getData()->numCacheMisses);
        allThreadsNumCacheRefs.fetch_add(it.getData()->numCacheRefs);
        allThreadsNumInstrs.fetch_add(it.getData()->numInstrs);

//        fprintf (thrData.output, "\n>>> Thread ID:     %lu\n", it.getKey());
//        fprintf (thrData.output, ">>> page faults    %ld\n", it.getData()->numFaults);
//        fprintf (thrData.output, ">>> TLB misses     %ld\n", it.getData()->numTlbMisses);
//        fprintf (thrData.output, ">>> cache misses   %ld\n", it.getData()->numCacheMisses);
//        fprintf (thrData.output, ">>> cache refs     %ld\n", it.getData()->numCacheRefs);
//        fprintf (thrData.output, ">>> num instr      %ld\n",it.getData()->numInstrs);

    }  
}

void writeAllocData () {

	unsigned print_new_address = new_address.load(relaxed);
	unsigned print_reused_address = reused_address.load(relaxed);
	unsigned print_num_free = num_free.load(relaxed);
	unsigned print_total_waits = total_waits.load(relaxed);

    fprintf (thrData.output, "\n>>>>> TOTALS <<<<<\n>>> num page faults    %u\n", allThreadsNumFaults.load(relaxed));
    fprintf (thrData.output, ">>> num TLB misses     %u\n", allThreadsNumTlbMisses.load(relaxed));
    fprintf (thrData.output, ">>> num cache misses   %u\n", allThreadsNumCacheMisses.load(relaxed));
    fprintf (thrData.output, ">>> num cache refs     %u\n", allThreadsNumCacheRefs.load(relaxed));
    fprintf (thrData.output, ">>> cache miss rate    %.2lf%%\n",
            (double)allThreadsNumCacheMisses.load(relaxed)
            /(double)allThreadsNumCacheRefs.load(relaxed) * 100 );
    fprintf (thrData.output, ">>> num instructions   %u\n", allThreadsNumInstrs.load(relaxed));
    fprintf (thrData.output, ">>> avg instr/alloc    %.2lf\n\n", (double)allThreadsNumInstrs.load(relaxed)
            /(double)(num_malloc.load(relaxed) + num_calloc.load(relaxed)
            + num_realloc.load(relaxed) + num_free.load(relaxed)));
	fprintf (thrData.output, ">>> num_malloc              %u\n", num_malloc.load(relaxed));
	fprintf (thrData.output, ">>> num_calloc              %u\n", num_calloc.load(relaxed));
	fprintf (thrData.output, ">>> num_realloc             %u\n", num_realloc.load(relaxed));
	fprintf (thrData.output, ">>> new_address             %u\n", print_new_address);
	fprintf (thrData.output, ">>> reused_address          %u\n", print_reused_address);
	fprintf (thrData.output, ">>> num_free                %u\n", print_num_free);
	fprintf (thrData.output, ">>> malloc_mmaps            %u\n", malloc_mmaps.load(relaxed));
	fprintf (thrData.output, ">>> total_mmaps             %u\n", total_mmaps.load(relaxed));
	fprintf (thrData.output, ">>> malloc_mmap_threshold   %zu\n", malloc_mmap_threshold);
	fprintf (thrData.output, ">>> threads                 %u\n", threads.load(relaxed));

	if (print_new_address)
		fprintf (thrData.output, ">>> cycles_new         %lu\n", (cycles_new.load(relaxed) / print_new_address));

	else
		fprintf (thrData.output, ">>> cycles_new         N/A\n");

	if (print_reused_address) 
		fprintf (thrData.output, ">>> cycles_reused      %lu\n",
				  (cycles_reused.load(relaxed) / print_reused_address));

	else
		fprintf (thrData.output, ">>> cycles_reused      N/A\n");

	if (num_free.load(relaxed) > 0)
		fprintf (thrData.output, ">>> cycles_free        %lu\n",
				  (cycles_free.load(relaxed) / num_free.load(relaxed)));

	else
		fprintf (thrData.output, ">>> cycles_free        N/A\n");

	fprintf (thrData.output, ">>> pthread_mutex_lock %u\n", num_pthread_mutex_locks.load(relaxed));
	fprintf (thrData.output, ">>> total_waits        %u\n", print_total_waits);
	fprintf (thrData.output, ">>> num_trylock        %u\n", num_trylock.load(relaxed));

	if (total_waits.load(relaxed) > 0) 
		fprintf (thrData.output, ">>> avg_wait_time  %lu\n",
				  (total_time_wait.load(relaxed)/total_waits.load(relaxed)));

	fprintf(thrData.output, ">>> madvise calls        %u\n", madvise_calls.load(relaxed));
    fprintf(thrData.output, ">>> w/ MADV_DONTNEED     %u\n", dontneed_advice_count.load(relaxed));
	fprintf(thrData.output, ">>> sbrk calls           %u\n", numSbrks.load(relaxed));
	fprintf(thrData.output, ">>> program size added   %u\n", programBreakChange.load(relaxed));

	fprintf (thrData.output, ">>> VmSize_start(kB)       %zu\n", vmInfo.VmSize_start);
	fprintf (thrData.output, ">>> VmSize_end(kB)         %zu\n", vmInfo.VmSize_end);
	fprintf (thrData.output, ">>> VmPeak(kB)             %zu\n", vmInfo.VmPeak);
	fprintf (thrData.output, ">>> VmRSS_start(kB)        %zu\n", vmInfo.VmRSS_start);
	fprintf (thrData.output, ">>> VmRSS_end(kB)          %zu\n", vmInfo.VmRSS_end);
	fprintf (thrData.output, ">>> VmHWM(kB)          %zu\n", vmInfo.VmHWM);
	fprintf (thrData.output, ">>> VmLib(kB)          %zu\n", vmInfo.VmLib);

	fprintf (thrData.output, "\n>>> num_mprotect             %u\n", num_mprotect.load(relaxed));
	fprintf (thrData.output, ">>> blowup_allocations       %u\n", blowup_allocations.load(relaxed));
	fprintf (thrData.output, ">>> blowup_bytes             %zu\n", blowup_bytes.load(relaxed));
	fprintf (thrData.output, ">>> fragmentation            %zu\n", fragmentation.load(relaxed));
//	fprintf (thrData.output, ">>> unused_mmap_region_count %zu\n", unused_mmap_region_count);
//	fprintf (thrData.output, ">>> unused_mmap_region_size  %#lx\n", unused_mmap_region_size);
//	fprintf (thrData.output, ">>> metadata_used_pages      %zu\n", metadata_used_pages);
	fprintf (thrData.output, ">>> metadata_object          %zu\n", metadata_object);
	fprintf (thrData.output, ">>> metadata_overhead        %zu\n", metadata_overhead);
	fprintf (thrData.output, ">>> totalSizeAlloc           %zu\n", totalSizeAlloc);
	fprintf (thrData.output, ">>> active_mem_HWM           %zu\n", active_mem_HWM.load(relaxed));
	fprintf (thrData.output, ">>> totalMemOverhead         %zu\n", totalMemOverhead);
	fprintf (thrData.output, ">>> memEfficiency            %.2f%%\n", memEfficiency);

	fprintf (thrData.output, "\n>>> summation_blowup_bytes         %zu\n", summation_blowup_bytes.load(relaxed));
	fprintf (thrData.output, ">>> summation_blowup_allocations   %u\n", summation_blowup_allocations.load(relaxed));

	writeAddressUsage();
	writeMappings();

	fflush (thrData.output);
}

void writeContention () {

	fprintf (thrData.output, "\n------------lock usage------------\n");
	for (auto lock = lockUsage.begin(); lock != lockUsage.end(); lock++) 
		fprintf (thrData.output, "lockAddr= %#lx  maxContention= %d\n",
					lock.getKey(), lock.getData()->maxContention);

	fprintf (thrData.output, "\n");
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
			"Region[%d]\norigin= %c, start= %#lx, end= %#lx, length= %zu, "
			"tid= %d, rw= %lu, allocations= %u\n\n",
			i, data->origin, data->start, data->end, data->length,
			data->tid, data->rw, data->allocations.load(relaxed));
		i++;
	}
}

//Get the freelist for this objects class size
Freelist* getFreelist (uint64_t size) {

	getFreelistLock.lock();
	Freelist* f = nullptr;

	for (auto s = csBegin; s != csEnd; s++) {
		uint64_t classSize = *s;
		if (size > classSize) {
			if (debug) printf ("%zu > %zu\n", size, classSize);
			continue;
		}
		else if (size <= classSize) {
			if (freelistMap.find(classSize, &f)) {
				if (debug) printf ("Freelist for size %zu found. Returning freelist[%zu]\n", size, classSize);
				break;
			}
			else {
				printf ("Didn't find Freelist for size %zu\n", size);
			}
		}
	}
	if (f == nullptr) {
		printf ("Can't find freelist, aborting\n");
		abort();
	}

	getFreelistLock.unlock();
	return f;
}

void calculateMemOverhead () {

	totalMemOverhead += fragmentation.load(relaxed);
	totalMemOverhead += blowup_bytes.load(relaxed);

	for (auto t = addressUsage.begin(); t != addressUsage.end(); t++) {
		auto data = t.getData();
		totalSizeAlloc += data->szTotal;
		totalSizeFree += data->szFreed;
		totalSizeDiff = totalSizeAlloc - totalSizeFree;
	}
	
	//Search the mmap regions for metadata
	/*
	if (bibop) {
		for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
			auto data = entry.getData();
			if (data->allocations.load(relaxed) > 0) {
				if (debug) printf ("ALLOCATED FROM: start= %#lx, end= %#lx, length= %#lx, allocations= %u\n",
									data->start, data->end, data->length, data->allocations.load(relaxed));
			}
			else {
				if (debug) printf ("Found an mmap region not used for allocations\n");
				unused_mmap_region_count++;
				unused_mmap_region_size += data->length;
				int n = num_used_pages(data->start, data->end);
				if (n > 0) metadata_used_pages += n;
				if (debug) printf ("NOT ALLOCATED FROM: start= %#lx, end= %#lx, length= %#lx, usedPages= %d\n",
									data->start, data->end, data->length, n);
			}
		}
		metadata_overhead = metadata_used_pages * 4096;
	}
	*/

	//Calculate metadata_overhead by using the per-object value
	long allocations = (num_malloc.load(relaxed) + num_calloc.load(relaxed) + num_realloc.load(relaxed));
	metadata_overhead = (allocations * metadata_object);

	totalMemOverhead += metadata_overhead;

	memEfficiency = ((float) totalSizeAlloc / (totalSizeAlloc + totalMemOverhead)) * 100;
	
	//Print all freelists
	/*
	for (auto entry = freelistMap.begin(); entry != freelistMap.end(); entry++) {
		auto freelist = entry.getData();
		auto key = entry.getKey();
		Freelist f = *freelist;
		printf ("Freelist[%zu]\n", key);
		for (auto e = f.begin(); e != f.end(); e++) {
			auto d = e.getData();
			printf ("object size= %zu\n", d->size);
		}
	}
	*/
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

//	RealX::free(moved);
	RealX::free(ptr2);

	metadata_object = metadata;
}

void get_bibop_metadata() {
	
	if (d_bibop_metadata) printf ("in get_bibop_metadata\n");

	bool done = false;
//	bool first = true;
	int bytes_index = 0;
	int bytes_max = 0;
	uintptr_t vpage = 0;
	unsigned add_pages = 0;
	unsigned bytes_in_use[100];


	for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
		if (done == true) break;
		auto data = entry.getData();
		if (data->allocations.load(relaxed) > 0) {
			if (d_bibop_metadata) {}
		}
		else {
			if (d_bibop_metadata) printf ("found unused mmap, start= %#lx, finding page..\n", data->start);
			add_pages = find_page (data->start, data->end);
			if (d_bibop_metadata) printf ("add_pages returned %d\n", add_pages);
			vpage = (data->start + (add_pages*4096));
			if (d_bibop_metadata) printf ("vpage= %#lx\n", vpage);
			unsigned bytes = search_vpage(vpage);
			if (bytes > 0) {
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

/*
inline bool isAllocatorInCallStack() {
		void * array[256];
		int frames = backtrace(array, 256);
		int allocatorLevel = -2;

		//char buf[256];

		if(frames >= 256) {
				fprintf(stderr, "WARNING: callstack may have been truncated\n");
		} else if(frames == 0) {
				fprintf(stderr, "WARNING: callstack depth was detected as zero\n");
		}

		if (debug) printf("backtrace, frames = %d:\n", frames);
		for(int i = 0; i < frames; i++) {
				void * addr = array[i];
				if (debug) printf("   level %3d: addr = %p\n", i, addr);

				if(selfmap::getInstance().isAllocator(addr)) {
						allocatorLevel = i;
				}
		}

		return((allocatorLevel != -2) && (allocatorLevel != frames - 1));
}
*/

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
		void * lastSeenAddress = NULL;
        int cur_depth = 0;

        while(((void *)prev_frame <= stackEnd) && (prev_frame >= current_frame) &&
				(cur_depth < CALLSITE_MAXIMUM_LENGTH)) {

			void * caller_address = prev_frame->caller_address;

			lastSeenAddress = caller_address;

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

	if((allocatorLevel > -1) && (allocatorLevel > 1))
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

void resetAtomics () {
	
	free_bytes = 0;
	summation_blowup_bytes = 0;
	summation_blowup_allocations = 0;
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

extern "C" int mprotect (void* addr, size_t len, int prot) {

	num_mprotect.fetch_add(1, relaxed);
	if (d_mprotect)
		printf ("mprotect/found= %s, addr= %p, len= %zu, prot= %d\n",
		mappingEditor(addr, len, prot) ? "true" : "false", addr, len, prot);

	return RealX::mprotect (addr, len, prot);
}

//extern "C" void* mremap (void* old_addr, size_t old_size, size_t new_size, int flags, ... /* void* new_addr */) {
//	printf ("%%mremap%%, old_addr= %p, old_size= %zu, new_size= %zu, flags= %d\n", old_addr, old_size, new_size, flags);
//
//	if (flags == MREMAP_MAYMOVE | MREMAP_FIXED) {
//		return RealX::mremap (old_addr, old_size, new_size, flags, 
//	}
//}

extern "C" void * yymmap(void *addr, size_t length, int prot, int flags,
				int fd, off_t offset) {
	if(initializer() == IN_PROGRESS) return RealX::mmap(addr, length, prot, flags, fd, offset);

	if (inMmap) return RealX::mmap (addr, length, prot, flags, fd, offset);

//	if (!selfmapInitialized) {
//		return RealX::mmap (addr, length, prot, flags, fd, offset);
//	}

//	if (prot == PROT_NONE) {
//		return RealX::mmap (addr, length, prot, flags, fd, offset);
//	}
	
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
		if (d_mmap) printf ("Unknown mmap: length= %zu, prot= %d\n", length, prot);
		mappingsLock.lock();
		mappings.insert(address, newMmapTuple(address, length, prot, 'u'));
		mappingsLock.unlock();
	}

	total_mmaps++;

	inMmap = false;
	return retval;
}
