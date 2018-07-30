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

#include <cstdlib>
#include <vector>
#include <atomic>
#include <cstdio>

#include "memsample.h"
#include "real.hh"
#include "xthreadx.hh"
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "spinlock.hh"
#include "selfmap.hh"

#define CALLSITE_MAXIMUM_LENGTH 10

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
uint64_t malloc_mmap_threshold = 0;

bool bumpPointer = false;
bool bibop = false;
bool inGetMmapThreshold = false;
bool usingRealloc = false;
bool mapsInitialized = false;
bool mmap_found = false;
bool gettingClassSizes = false;
//const bool debug = true;
const bool debug = false;

uint64_t totalSizeAlloc = 0;
uint64_t totalSizeFree = 0;
uint64_t totalSizeDiff = 0;
uint64_t totalMemOverhead = 0;
uint64_t unused_mmap_region_size = 0;
uint64_t unused_mmap_region_count = 0;
float memEfficiency = 0;

//Atomic Globals
std::atomic_uint allThreadsNumFaults (0);
std::atomic_uint allThreadsNumTlbMisses (0);
std::atomic_uint allThreadsNumCacheMisses (0);
std::atomic_uint allThreadsNumCacheRefs (0);
std::atomic_uint allThreadsNumInstrs (0);
std::atomic_uint numFrees (0);
std::atomic_uint numMallocs (0);
std::atomic_uint numCallocs (0);
std::atomic_uint numReallocs (0);
std::atomic_uint new_address (0);
std::atomic_uint reused_address (0);
std::atomic_uint num_pthread_mutex_locks (0);
std::atomic_uint trylockAttempts (0);
std::atomic_uint totalWaits (0);
std::atomic_uint totalTimeWaiting (0);
std::atomic_uint dontneed_advice_count (0);
std::atomic_uint madvise_calls (0);
std::atomic_uint numSbrks (0);
std::atomic_uint programBreakChange (0);
std::atomic_uint malloc_mmaps (0);
std::atomic_uint total_mmaps (0);
std::atomic_uint numTempAllocs (0);
std::atomic_uint tmppos (0);
std::atomic_uint cyclesFree (0);
std::atomic_uint cyclesNewAlloc (0);
std::atomic_uint cyclesReuseAlloc (0);
std::atomic_uint numThreads (0);
std::atomic_uint blowup_bytes (0);
std::atomic_uint blowup_allocations (0);
std::atomic_uint fragmentation (0);

//Vector of class sizes
std::vector <uint64_t> classSizes;
std::vector<uint64_t>::iterator csBegin, csEnd;
size_t largestClassSize;

//Struct of virtual memory info
VmInfo vmInfo;

#define relaxed std::memory_order_relaxed
#define acquire std::memory_order_acquire
#define release std::memory_order_release
//#define TEMP_MEM_SIZE 16384000
#define TEMP_MEM_SIZE 65536000

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
//addr, numAccesses, szTotal, szFreed, szUsed, numAllocs, numFrees
HashMap <uint64_t, ObjectTuple*, spinlock> addressUsage;

//Hashmap of lock addr to LC
//contention, maxContention
HashMap <uint64_t, LC*, spinlock> lockUsage;

//Hashmap of mmap addrs to tuple:
//start, end, length, used
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
MmapTuple* newMmapTuple (uint64_t address, size_t length);
LC* newLC ();
thread_alloc_data* newTad();
void getMemUsageStart ();
void getMemUsageEnd ();
void printMetadata ();
void printAddressUsage ();
void globalizeThreadAllocData();
void getMetadata ();
void test();
void test2();
FreeObject* newFreeObject (uint64_t addr, uint64_t size);
Freelist* getFreelist (uint64_t size);
void calculateMemOverhead ();
void printMappings();

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
	//Get metadata
//	test();
//	getMetadata();

    allocator_name = (char *) myMalloc(100 * sizeof(char));

	selfmap::getInstance().getTextRegions();

	mallocInitialized = INITIALIZED;
	void * program_break = sbrk(0);
	thrData.stackStart = (char *)__builtin_frame_address(0);
	thrData.stackEnd = (char *)__libc_stack_end;

	// Generate the name of our output file, then open it for writing.
	char outputFile[MAX_FILENAME_LEN];
	pid_t pid = getpid();
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
    

	// Determines allocator style: bump pointer or bibop
	getAllocStyle ();
	
	// Determine class size for bibop allocator
	// If the class sizes are in the class_sizes.txt file
	// it will read from that. Else if will get the info
	if (bibop) getClassSizes ();
	else {
		getMmapThreshold();
		Freelist* f = (Freelist*) myMalloc (sizeof (Freelist));
		f->initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);
		freelistMap.insert(0, f);
		if (debug) printf ("Inserting freelistMap[0]\n");
	}

	getMemUsageStart();

	return mallocInitialized;
}

__attribute__((destructor)) void finalizer_mallocprof () {
	fclose(thrData.output);
}

void exitHandler() {

	doPerfRead();
//	test2();
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
		if(mallocInitialized != INITIALIZED) {
			if((tmppos + sz) < TEMP_MEM_SIZE) 
				return myMalloc (sz);
			else {
				fprintf(stderr, "error: temp allocator out of memory\n");
				fprintf(stderr, "\t requested size = %zu, total size = %d, total allocs = %u\n",
						  sz, TEMP_MEM_SIZE, numTempAllocs.load(relaxed));
				abort();
			}
		}
	
		if (!mapsInitialized) return RealX::malloc (sz);

		//If already there, then just pass the malloc to RealX
		if (inAllocation) return RealX::malloc (sz);

		//If in our getClassSizes function, use RealX
		//We need this because the vector uses push_back
		//which calls malloc, also fopen calls malloc
		if(gettingClassSizes) return RealX::malloc(sz);

        if(!samplingInit){
            initSampling();
            samplingInit = true;
        }

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
				if (freelistMap.find(0, &freelist)) {
					if (debug) printf ("FreelistMap[0] found\n");
					for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
						auto data = f.getData();
						if (sz <= data->size) reuseFreePossible = true;
						if (address == data->addr) {
							reuseFreeObject = true;
							(*freelist).erase(address);
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
				freelist = getFreelist (sz);
				if ((*freelist).begin() != (*freelist).end()) {
					reuseFreePossible = true;
					for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
						auto data = f.getData();
						if (address == data->addr) {
							reuseFreeObject = true;
							(*freelist).erase(address);
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
			cyclesReuseAlloc.fetch_add (cyclesForMalloc, relaxed);
		}

		else {
			new_address.fetch_add (1, relaxed);
			cyclesNewAlloc.fetch_add (cyclesForMalloc, relaxed);
			addressUsage.insertIfAbsent (address, newObjectTuple(address, sz));
		}
		
		numMallocs.fetch_add(1, relaxed);

        tadMap.insertIfAbsent(tid, newTad()); 
        thread_alloc_data *tad;
        
        if (tadMap.find(tid, &tad)){
            
            tad->numFaults += after_faults - before_faults;
            tad->numTlbMisses += after_tlb_misses - before_tlb_misses;
            tad->numCacheMisses += after_cache_misses - before_cache_misses;
            tad->numCacheRefs += after_cache_refs - before_cache_refs;
            tad->numInstrs += after_instrs - before_instrs;

        }

        if(after_instrs - before_instrs != 0){
            fprintf(stderr, "Malloc from thread %lu\n", tid);
            fprintf(stderr, "Num Faults:        %ld\n", after_faults - before_faults);
            fprintf(stderr, "Num TLB Misses:    %ld\n", after_tlb_misses - before_tlb_misses);
            fprintf(stderr, "Num Cache Misses:  %ld\n", after_cache_misses - before_cache_misses);
            fprintf(stderr, "Num Cache Refs:    %ld\n", after_cache_refs - before_cache_refs);
            fprintf(stderr, "Num Insructions:   %ld\n\n", after_instrs - before_instrs);
        }

        //thread_local
		mappingsLock.lock();
		for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
			auto data = entry.getData();
			if (data->used == true) continue;
			if (data->start <= address && address <= data->end) {
				if (debug) printf ("allocating from mmapped region\n");
				data->used = true;
				break;
			}
		}
		mappingsLock.unlock();

		//thread_local
		inAllocation = false;

		return objAlloc;
	}

	void * yycalloc(size_t nelem, size_t elsize) {

		if(!mapsInitialized || inAllocation){

			void * ptr = NULL;
			ptr = malloc (nelem * elsize);
			if(ptr)
				memset(ptr, 0, nelem * elsize);
			return ptr;
		}

        if(!samplingInit){
            initSampling();
            samplingInit = true;
        }

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

		//Check for Mem Blowup
		if (sz <= malloc_mmap_threshold) {
			freelistLock.lock();
			Freelist* freelist;
			bool reuseFreePossible = false;
			bool reuseFreeObject = false;
		
			if (!bibop) {
				if (freelistMap.find(0, &freelist)) {
					if (debug) printf ("FreelistMap[0] found\n");
					for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
						auto data = f.getData();
						if (sz <= data->size) reuseFreePossible = true;
						if (address == data->addr) {
							reuseFreeObject = true;
							(*freelist).erase(address);
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
				freelist = getFreelist (sz);
				if ((*freelist).begin() != (*freelist).end()) {
					reuseFreePossible = true;
					for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
						auto data = f.getData();
						if (address == data->addr) {
							reuseFreeObject = true;
							(*freelist).erase(address);
							break;
						}
					}
				}
			}

			freelistLock.unlock();

			//If Reuse was possible, but didn't reuse
			if (reuseFreePossible && !reuseFreeObject) {
				//possible memory blowup has occurred
				//get size of allocation
				blowup_bytes.fetch_add(sz, relaxed);
				//increment counter
				blowup_allocations.fetch_add(1, relaxed);
			}
		}//End Check for Mem Blowup

		ObjectTuple* t;
		if ( addressUsage.find(address, &t) ){
			t->numAccesses++;
			t->szTotal += (nelem*elsize);
			t->szUsed = (nelem*elsize);
			t->numAllocs++;
			reused_address.fetch_add(1, relaxed);
			cyclesReuseAlloc.fetch_add(cyclesForCalloc, relaxed);
      }
      
		else{
			new_address.fetch_add (1, relaxed);
			cyclesNewAlloc.fetch_add (cyclesForCalloc, relaxed);
			addressUsage.insertIfAbsent(address, newObjectTuple(address, (nelem*elsize)));
      }

		numCallocs.fetch_add (1, relaxed);

        tadMap.insertIfAbsent(tid, newTad()); 
        thread_alloc_data *tad;
        
        if (tadMap.find(tid, &tad)){
            
            tad->numFaults += after_faults - before_faults;
            tad->numTlbMisses += after_tlb_misses - before_tlb_misses;
            tad->numCacheMisses += after_cache_misses - before_cache_misses;
            tad->numCacheRefs += after_cache_refs - before_cache_refs;
            tad->numInstrs += after_instrs - before_instrs; 

        }

        if(after_instrs - before_instrs != 0){
            fprintf(stderr, "Calloc from thread %lu\n", tid);
            fprintf(stderr, "Num Faults:        %ld\n", after_faults - before_faults);
            fprintf(stderr, "Num TLB Misses:    %ld\n", after_tlb_misses - before_tlb_misses);
            fprintf(stderr, "Num Cache Misses:  %ld\n", after_cache_misses - before_cache_misses);
            fprintf(stderr, "Num Cache Refs:    %ld\n", after_cache_refs - before_cache_refs);
            fprintf(stderr, "Num Insructions:   %ld\n\n", after_instrs - before_instrs);
        }
		
        mappingsLock.lock();
		for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
			auto data = entry.getData();
			if (data->used == true) continue;
			if (data->start <= address && address <= data->end) {
				printf ("allocating from mmapped region\n");
				data->used = true;
				break;
			}
		}
		mappingsLock.unlock();

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

		// Determine whether the specified object came from our global buffer;
		// only call RealX::free() if the object did not come from here.
		if((ptr >= (void *) tmpbuf) && (ptr <= (void *)(tmpbuf + TEMP_MEM_SIZE))) 
				myFree (ptr);

		else {

			uint64_t address = reinterpret_cast <uint64_t> (ptr);
			ObjectTuple* t;

			if (addressUsage.find(address, &t)){
				t->szFreed += t->szUsed;
				t->numAccesses++;
				t->numFrees++;

				//Add this object to it's Freelist
				//If bump pointer, add to freelistMap[0]
				if (t->szUsed <= malloc_mmap_threshold) {
					freelistLock.lock();
					if (!bibop) {
						Freelist* f;
						if (freelistMap.find(0, &f)) {
							(*f).insert(address, newFreeObject(address, t->szUsed));
						}
						else {
							printf ("Freelist[0] not found.\n");
							abort ();
						}
					}

					//If bibop, add to freelistMap[t->szUsed]
					else {
						Freelist* f = getFreelist(t->szUsed);
						(*f).insert(address, newFreeObject(address, t->szUsed));
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
			numFrees.fetch_add(1, relaxed);
			cyclesFree.fetch_add ((after - before), relaxed);

            tadMap.insertIfAbsent(tid, newTad()); 
            thread_alloc_data *tad;
            
            if (tadMap.find(tid, &tad)){
                
                tad->numFaults += after_faults - before_faults;
                tad->numTlbMisses += after_tlb_misses - before_tlb_misses;
                tad->numCacheMisses += after_cache_misses - before_cache_misses;
                tad->numCacheRefs += after_cache_refs - before_cache_refs;
                tad->numInstrs += after_instrs - before_instrs; 

            }


            if(after_instrs - before_instrs != 0){
                fprintf(stderr, "Free from thread %lu\n", tid);
                fprintf(stderr, "Num Faults:        %ld\n", after_faults - before_faults);
                fprintf(stderr, "Num TLB Misses:    %ld\n", after_tlb_misses - before_tlb_misses);
                fprintf(stderr, "Num Cache Misses:  %ld\n", after_cache_misses - before_cache_misses);
                fprintf(stderr, "Num Cache Refs:    %ld\n", after_cache_refs - before_cache_refs);
                fprintf(stderr, "Num Insructions:   %ld\n\n", after_instrs - before_instrs);
            }

		}
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
			if(ptr == NULL)
				return yymalloc(sz);

			yyfree(ptr);
			return yymalloc(sz);
		}

        if(!samplingInit){
            initSampling();
            samplingInit = true;
        }

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

		//Check for Mem Blowup
		if (sz <= malloc_mmap_threshold) {
			freelistLock.lock();
			Freelist* freelist;
			bool reuseFreePossible = false;
			bool reuseFreeObject = false;
		
			if (!bibop) {
				if (freelistMap.find(0, &freelist)) {
					if (debug) printf ("FreelistMap[0] found\n");
					for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
						auto data = f.getData();
						if (sz <= data->size) reuseFreePossible = true;
						if (address == data->addr) {
							reuseFreeObject = true;
							(*freelist).erase(address);
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
				freelist = getFreelist (sz);
				if ((*freelist).begin() != (*freelist).end()) {
					reuseFreePossible = true;
					for (auto f = (*freelist).begin(); f != (*freelist).end(); f++) {
						auto data = f.getData();
						if (address == data->addr) {
							reuseFreeObject = true;
							(*freelist).erase(address);
							break;
						}
					}
				}
			}

			freelistLock.unlock();

			//If Reuse was possible, but didn't reuse
			if (reuseFreePossible && !reuseFreeObject) {
				//possible memory blowup has occurred
				//get size of allocation
				blowup_bytes.fetch_add(sz, relaxed);
				//increment counter
				blowup_allocations.fetch_add(1, relaxed);
			}
		}//End Check for Mem Blowup

		ObjectTuple* t;
      if ( addressUsage.find(address, &t) ){
			t->numAccesses++;
			t->szTotal += sz;
			t->szUsed = sz;
			t->numAllocs++;
			reused_address.fetch_add(1, relaxed);
			cyclesReuseAlloc.fetch_add (cyclesForRealloc, relaxed);
      }
		
		else{
			new_address.fetch_add(1, relaxed);
			cyclesNewAlloc.fetch_add (cyclesForRealloc, relaxed);
			addressUsage.insertIfAbsent(address, newObjectTuple(address, sz));
      }

		numReallocs.fetch_add(1, relaxed);
        
        tadMap.insertIfAbsent(tid, newTad()); 
        thread_alloc_data *tad;
        
        if (tadMap.find(tid, &tad)){
            
            tad->numFaults += after_faults - before_faults;
            tad->numTlbMisses += after_tlb_misses - before_tlb_misses;
            tad->numCacheMisses += after_cache_misses - before_cache_misses;
            tad->numCacheRefs += after_cache_refs - before_cache_refs;
            tad->numInstrs += after_instrs - before_instrs; 

        }

        if(after_instrs - before_instrs != 0){
            fprintf(stderr, "Realloc from thread %lu\n", tid);
            fprintf(stderr, "Num Faults:        %ld\n", after_faults - before_faults);
            fprintf(stderr, "Num TLB Misses:    %ld\n", after_tlb_misses - before_tlb_misses);
            fprintf(stderr, "Num Cache Misses:  %ld\n", after_cache_misses - before_cache_misses);
            fprintf(stderr, "Num Cache Refs:    %ld\n", after_cache_refs - before_cache_refs);
            fprintf(stderr, "Num Insructions:   %ld\n\n", after_instrs - before_instrs);
        }
		
        mappingsLock.lock();
		for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
			auto data = entry.getData();
			if (data->used == true) continue;
			if (data->start <= address && address <= data->end) {
				printf ("allocating from mmapped region\n");
				data->used = true;
				break;
			}
		}
		mappingsLock.unlock();

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
		numThreads.fetch_add(1, relaxed);
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
					totalWaits.fetch_add(1, relaxed);
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
			totalTimeWaiting.fetch_add (timeWaiting, relaxed);
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
			trylockAttempts.fetch_add(1, relaxed);
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

MmapTuple* newMmapTuple (uint64_t address, size_t length) {

	MmapTuple* t = (MmapTuple*) myMalloc (sizeof (MmapTuple));	

	uint64_t end = (address + length);
	t->start = address;
	t->end = end;
	t->length = length;
	t->used = false;
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
	if((tmppos + size) < TEMP_MEM_SIZE) {
		retptr = (void *)(tmpbuf + tmppos);
		tmppos.fetch_add (size, relaxed);
		numTempAllocs++;
	} else {
		fprintf(stderr, "error: global allocator out of memory\n");
		fprintf(stderr, "\t requested size = %zu, total size = %d, "
							 "total allocs = %u\n", size, TEMP_MEM_SIZE, numTempAllocs.load(relaxed));
		abort();
	}
	temp_mem_lock.unlock ();
	return retptr;
}

void myFree (void* ptr) {

	if (ptr == NULL) return;	

	temp_mem_lock.lock();
	if ((ptr >= tmpbuf) && (ptr <= (tmpbuf + TEMP_MEM_SIZE))) {
		numTempAllocs--;
		if(numTempAllocs == 0) tmppos = 0;
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
	void* add2 = RealX::malloc (2048);

	long address1 = reinterpret_cast<long> (add1);
	long address2 = reinterpret_cast<long> (add2);

	RealX::free (add1);
	RealX::free (add2);

	long address1Page = address1 / 4096;
	long address2Page = address2 / 4096;

	if ((address1Page - address2Page) != 0) {

		bibop = true;
		fprintf (thrData.output, ">>> allocator     bibop\n");
	}

	else {

		bumpPointer = true;
		fprintf (thrData.output, ">>> allocator     bump-pointer\n");
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
			if (newAddr != oldAddr)
				classSizes.push_back (oldSize);

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

    fprintf (thrData.output, "\n>>>>> TOTALS <<<<<\n>>> num page faults    %u\n", allThreadsNumFaults.load(relaxed));
    fprintf (thrData.output, ">>> num TLB misses     %u\n", allThreadsNumTlbMisses.load(relaxed));
    fprintf (thrData.output, ">>> num cache misses   %u\n", allThreadsNumCacheMisses.load(relaxed));
    fprintf (thrData.output, ">>> num cache refs     %u\n", allThreadsNumCacheRefs.load(relaxed));
    fprintf (thrData.output, ">>> cache miss rate    %.2lf%%\n",
            (double)allThreadsNumCacheMisses.load(relaxed)
            /(double)allThreadsNumCacheRefs.load(relaxed) * 100 );
    fprintf (thrData.output, ">>> num instructions   %u\n", allThreadsNumInstrs.load(relaxed));
    fprintf (thrData.output, ">>> avg instr/alloc    %.2lf\n\n", (double)allThreadsNumInstrs.load(relaxed)
            /(double)(numMallocs.load(relaxed) + numCallocs.load(relaxed)
            + numReallocs.load(relaxed) + numFrees.load(relaxed)));
	fprintf (thrData.output, ">>> mallocs            %u\n", numMallocs.load(relaxed));
	fprintf (thrData.output, ">>> callocs            %u\n", numCallocs.load(relaxed));
	fprintf (thrData.output, ">>> reallocs           %u\n", numReallocs.load(relaxed));
	fprintf (thrData.output, ">>> new_address        %u\n", new_address.load(relaxed));
	fprintf (thrData.output, ">>> reused_address     %u\n", reused_address.load(relaxed));
	fprintf (thrData.output, ">>> frees              %u\n", numFrees.load(relaxed));
	fprintf (thrData.output, ">>> malloc_mmaps       %u\n", malloc_mmaps.load(relaxed));
	fprintf (thrData.output, ">>> total_mmaps        %u\n", total_mmaps.load(relaxed));
	fprintf (thrData.output, ">>> malloc_mmap_threshold   %zu\n", malloc_mmap_threshold);

	if (new_address.load(relaxed) > 0)
		fprintf (thrData.output, ">>> cyclesNewAlloc     %u\n",
				  (cyclesNewAlloc.load(relaxed) / new_address.load(relaxed)));

	else
		fprintf (thrData.output, ">>> cyclesNewAlloc     N/A\n");

	if (reused_address.load(relaxed) > 0) 
		fprintf (thrData.output, ">>> cyclesReuseAlloc   %u\n",
				  (cyclesReuseAlloc.load(relaxed) / reused_address.load(relaxed)));

	else fprintf (thrData.output, ">>> cyclesReuseAlloc   N/A\n");

	if (numFrees.load(relaxed) > 0)
		fprintf (thrData.output, ">>> cyclesFree         %u\n",
				  (cyclesFree.load(relaxed) / numFrees.load(relaxed)));

	else fprintf (thrData.output, ">>> cyclesFree         N/A\n");

	fprintf (thrData.output, ">>> pthread_mutex_lock %u\n", num_pthread_mutex_locks.load(relaxed));
	fprintf (thrData.output, ">>> totalWaits         %u\n", totalWaits.load(relaxed));
	fprintf (thrData.output, ">>> trylockAttemps     %u\n", trylockAttempts.load(relaxed));

	if (totalWaits.load(relaxed) > 0) 
		fprintf (thrData.output, ">>> avgWaitTime        %u\n",
				  (totalTimeWaiting.load(relaxed)/totalWaits.load(relaxed)));

	fprintf (thrData.output, ">>> numThreads         %u\n", numThreads.load(relaxed));
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

	fprintf (thrData.output, "\n>>> blowup_allocations   %u\n", blowup_allocations.load(relaxed));
	fprintf (thrData.output, ">>> blowup_bytes         %u\n", blowup_bytes.load(relaxed));

	fprintf (thrData.output, ">>> fragmentation            %u\n", fragmentation.load(relaxed));
	fprintf (thrData.output, ">>> totalMemOverhead         %zu\n", totalMemOverhead);
	fprintf (thrData.output, ">>> unused_mmap_region_count %zu\n", unused_mmap_region_count);
	fprintf (thrData.output, ">>> unused_mmap_region_size  %zu\n", unused_mmap_region_size);
	fprintf (thrData.output, ">>> totalSizeAlloc           %zu\n", totalSizeAlloc);
	fprintf (thrData.output, ">>> totalMemUsed             %zu\n", totalSizeAlloc + totalMemOverhead + unused_mmap_region_size);
	fprintf (thrData.output, ">>> memEfficiency            %d%%\n", (int)memEfficiency);

	printAddressUsage();
	printMappings();

	fflush (thrData.output);
}

void writeContention () {
	pid_t pid = getpid ();
	char outputFile[MAX_FILENAME_LEN];
	snprintf(outputFile, MAX_FILENAME_LEN, "%s_contention_pid_%d.txt",
		program_invocation_name, pid);
	FILE* file = fopen (outputFile, "w");

	auto mapEnd = lockUsage.end();
	for (auto lock = lockUsage.begin(); lock != mapEnd; lock++) 
		fprintf (file, "lockAddr= %zx  maxContention= %d\n", lock.getKey(), lock.getData()->maxContention);

	fflush (file);
	fclose (file);
}

void printMetadata () {

}

void printAddressUsage () {

	fprintf (thrData.output, "\n----------memory usage----------\n");

	for (auto t = addressUsage.begin(); t != addressUsage.end(); t++) {
		auto data = t.getData();
		fprintf (thrData.output, ">>> addr:0x%zx numAccesses:%zu szTotal:%zu "
				"szFreed:%zu numAllocs:%u numFrees:%u\n",
				data->addr, data->numAccesses, data->szTotal,
				data->szFreed, data->numAllocs, data->numFrees);
	}
}

void getMetadata () {

	pid_t id = ::getpid();
	char fileName[30];

	printf ("pid = %d\n", id);
	//getchar();
	std::snprintf (fileName, 30, "/proc/%d/status", id);
	std::ifstream file (fileName);
	std::string line;
	
	int beforeMallocRSS = 0, afterMallocRSS = 0;
	const size_t size = 16;
	const int numM = 2305;
	unsigned int allocatedBytes = size*numM;
	struct s {
		uint64_t first;
		uint64_t second;
	};
	struct s* p;
	void** addr = (void**) myMalloc(numM*sizeof(void*));
	int j = 0;

	for (int i = 0; i <= 41; i++) {

		std::getline (file, line);
		if (line.find("VmRSS") != std::string::npos) {
			std::sscanf (line.data(), "%*s%d%*s", &beforeMallocRSS);
			break;
		}
	}
	file.close();

	for (int i = 0; i < numM; i++) {

		p = (struct s*) RealX::malloc(size);
		addr[j] = (void*) p;
		j++;
		p->first = 100;
		p->second = 200;
	}

	//getchar();

	file.open(fileName);

	for (int i = 0; i <= 41; i++) {

		std::getline (file, line);
		if (line.find("VmRSS") != std::string::npos) {
			std::sscanf (line.data(), "%*s%d%*s", &afterMallocRSS);
			break;
		}
	}

	file.close();

	for (int i = 0; i < numM; i++) {
		RealX::free(addr[i]);
	}
	
	printf ("beforeMallocRSS=     %d kB\n"
			  "afterMallocRSS=      %d kB\n"
			  "allocatedBytes=      %u B\n",
			  beforeMallocRSS, afterMallocRSS, allocatedBytes);
}

void test() {

	pid_t id = ::getpid();
	char fileName[30];

	size_t size = 4;
	int* p;

	printf ("pid = %d\n", id);
	std::snprintf (fileName, 30, "/proc/%d/status", id);

	int beforeRSS = 0;
	int afterRSS = 0;
	int beforeVmSize = 0;
	int afterVmSize = 0;
	std::string line;

	std::ifstream file (fileName);
	for (int i = 0; i <= 41; i++) {

		std::getline (file, line);
		if (line.find("VmSize") != std::string::npos) {
			std::sscanf (line.data(), "%*s%d%*s", &beforeVmSize);
			continue;
		}
		else if (line.find("VmRSS") != std::string::npos) {
			std::sscanf (line.data(), "%*s%d%*s", &beforeRSS);
			break;;
		}
	}
	file.close();

	p = (int*) RealX::malloc (size);
//	*p = 100;

	file.open(fileName);
	for (int i = 0; i <= 41; i++) {

		std::getline (file, line);
		if (line.find("VmSize") != std::string::npos) {
			std::sscanf (line.data(), "%*s%d%*s", &afterVmSize);
			continue;
		}
		else if (line.find("VmRSS") != std::string::npos) {
			std::sscanf (line.data(), "%*s%d%*s", &afterRSS);
			break;;
		}
	}
	file.close();

	RealX::free (p);

	printf ("beforeVmSize=    %d kB\n"
			  "afterVmSize=     %d kB\n"
			  "beforeRSS=       %d kB\n"
			  "afterRSS=        %d kB\n",
			  beforeVmSize, afterVmSize, beforeRSS, afterRSS);
}

void test2() {

	for (auto s = csBegin; s != csEnd; s++) {
		void* p = yymalloc (*s);
		yyfree (p);
	}
}

void printMappings () {

	int i = 0;
	fprintf (thrData.output, "\n----------  mappings  ----------\n");
	for (auto r = mappings.begin(); r != mappings.end(); r++) {
		auto data = r.getData();
		fprintf (thrData.output, "Region[%d]\nstart= 0x%zx, end= 0x%zx, length= 0x%zx, used= %s\n\n", i, data->start, data->end, data->length, data->used ? "true":"false");
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

	for (auto entry = mappings.begin(); entry != mappings.end(); entry++) {
		auto data = entry.getData();
		if (data->used == true) continue;
		else {
			if (debug) printf ("Found an mmap region not used for allocations\n");
			unused_mmap_region_count++;
			unused_mmap_region_size += data->length;
		}
	}

//	memEfficiency = ((float) totalSizeAlloc / (totalSizeAlloc + totalMemOverhead + unused_mmap_region_size)) * 100;
	memEfficiency = ((float) totalSizeAlloc / (totalSizeAlloc + totalMemOverhead)) * 100;
}

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
/*
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
        while(((void *)prev_frame <= stackEnd) &&
                (prev_frame >= current_frame) && (cur_depth < CALLSITE_MAXIMUM_LENGTH)) {
						void * caller_address = prev_frame->caller_address;
						lastSeenAddress = caller_address;

						if(selfmap::getInstance().isAllocator(caller_address)) {
								printf("current caller is allocator: caller = %p, frame = %p\n", caller_address, prev_frame);
								//return true;
								//hasSeenAllocator = true;
								allocatorLevel = cur_depth;
						} else {
								printf("current caller is NOT the allocator: caller = %p, frame = %p\n", caller_address, prev_frame);
						}

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

				printf("allocatorLevel = %d, cur_depth = %d\n", allocatorLevel, cur_depth);
				if((allocatorLevel > -1) && (allocatorLevel < cur_depth - 1)) {
				//if(hasSeenAllocator && !selfmap::getInstance().isAllocator(lastSeenAddress)) {
						return true;
				}	

				return false;
}
*/

extern "C" void * yymmap(void *addr, size_t length, int prot, int flags,
				int fd, off_t offset) {
	if(initializer() == IN_PROGRESS) return RealX::mmap(addr, length, prot, flags, fd, offset);

//	if (!mapsInitialized) return RealX::mmap (addr, length, prot, flags, fd, offset);

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
	if (inAllocation) malloc_mmaps.fetch_add (1, relaxed);

	total_mmaps.fetch_add (1, relaxed);

	if (!mapsInitialized) {
		inMmap = false;
		return retval;
	}

	mappingsLock.lock();
	mappings.insert(address, newMmapTuple(address, length));
	mappingsLock.unlock();

	inMmap = false;
	return retval;
}
