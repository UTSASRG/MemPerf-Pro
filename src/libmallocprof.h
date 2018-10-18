#ifndef __LIBMALLOCPROF_H__
#define __LIBMALLOCPROF_H__

#include "memsample.h"

#define relaxed std::memory_order_relaxed
#define acquire std::memory_order_acquire
#define release std::memory_order_release
#define CALLSITE_MAXIMUM_LENGTH 20
#define ENTRY_SIZE 8

//For libc, bump pointer
#define LARGE_OBJECT 512
#define SMALL_OBJECT 0

//Bump-pointer key to overhead hashmap
#define BP_OVERHEAD 0

#define PAGE_BITS 12 
#define TEMP_MEM_SIZE 1024 * 1024 * 1024 //1GB
#define MAX_CLASS_SIZE 1050000


//Structures
typedef struct {
	uint64_t tid = 0;
	uint64_t mutex_waits = 0;
	uint64_t mutex_wait_cycles = 0;
	uint64_t mutex_trylock_fails = 0;
	uint64_t mmap_waits = 0;
	uint64_t mmap_wait_cycles = 0;
	uint64_t sbrk_waits = 0;
	uint64_t sbrk_wait_cycles = 0;
	uint64_t madvise_waits = 0;
	uint64_t madvise_wait_cycles = 0;
} ThreadContention;

typedef struct {
	uint64_t addr;
	size_t size;
} FreeObject;

typedef struct {
	uint64_t addr;
	uint64_t numAccesses;
	size_t szTotal;
	size_t szFreed;
	size_t szUsed;
	uint64_t numAllocs;
	uint64_t numFrees;
} ObjectTuple;

typedef struct {
	uint64_t start;
	uint64_t end;
	size_t length;
	uint64_t rw;
	char origin;
	pid_t tid;
	std::atomic_uint allocations;
} MmapTuple;

typedef struct {
	std::atomic_size_t metadata;
	std::atomic_size_t blowup;
	std::atomic_size_t alignment;
	void addMetadata(size_t size) {metadata.fetch_add(size, relaxed);}
	void addBlowup(size_t size) {blowup.fetch_add(size, relaxed);}
	void addAlignment(size_t size) {alignment.fetch_add(size, relaxed);}
	size_t getMetadata() {
		size_t temp = metadata.load();
		return temp;
	}
	size_t getBlowup() {
		size_t temp = blowup.load();
		return temp;
	}
	size_t getAlignment() {
		size_t temp = alignment.load();
		return temp;
	}
	void init() {
		metadata = 0;
		blowup = 0;
		alignment = 0;
	}
} Overhead;

typedef struct {
	size_t VmSize_start;
	size_t VmSize_end;
	size_t VmPeak;
	size_t VmRSS_start;
	size_t VmRSS_end;
	size_t VmHWM;
	size_t VmLib;
} VmInfo;

typedef struct {
	uint64_t faults = 0;
	uint64_t tlb_read_misses = 0;
	uint64_t tlb_write_misses = 0;
	uint64_t cache_misses = 0;
	uint64_t cache_refs = 0;
	uint64_t instructions = 0;
} PerfReadInfo;

typedef struct LockContention {
	int contention;
	int maxContention;
} LC;

struct stack_frame {
	struct stack_frame * prev;	// pointing to previous stack_frame
	void * caller_address;		// the address of caller
};

//Enumerations
enum initStatus{            //enum to keep track of libmallocprof's constuction status 
    INIT_ERROR = -1,        //allocation calls are just passed on to RealX if libmallocprof isn't ready
    NOT_INITIALIZED = 0, 
    IN_PROGRESS = 1, 
    INITIALIZED = 2
};


/*
 * AllocType
 *
 *    Tracks which type of metadata should we obtain and
 *    what to add to the collected metadata
 */
enum memAllocType {
	REALLOC,
	MALLOC,
	CALLOC,
	FREE
};


typedef struct  {
	bool reused;
	pid_t tid;
	PerfReadInfo before;
	PerfReadInfo after;
	size_t size;
	size_t classSize;
	uint64_t cycles;
	uint64_t address;
	uint64_t tsc_before;
	uint64_t tsc_after;
	enum memAllocType type;
	thread_alloc_data *tad;
} allocation_metadata;


// Functions 
bool mappingEditor (void* addr, size_t len, int prot);
inline bool isAllocatorInCallStack();
inline size_t getClassSizeFor(size_t size);
int find_pages(uintptr_t vstart, uintptr_t vend, unsigned long[]);
int num_used_pages(uintptr_t vstart, uintptr_t vend);
// void analyzePerfInfo(PerfReadInfo*, PerfReadInfo*, size_t, bool*, pid_t);
void analyzePerfInfo(allocation_metadata *metadata);
// void analyzeAllocation(size_t size, uint64_t address, uint64_t cycles, size_t, bool*);
void analyzeAllocation(allocation_metadata *metadata);
size_t analyzeFree(uint64_t);
void calculateMemOverhead ();
void clearFreelists();
void doBefore(allocation_metadata *metadata);
void doAfter(allocation_metadata *metadata);
void get_bp_metadata();
void get_bibop_metadata();
void getAddressUsage(size_t size, uint64_t address, size_t classSize, uint64_t cycles);
void getAllocStyle ();
void getAlignment(size_t size, size_t classSize);
//void getBlowup(size_t size, uint64_t address, size_t classSize, bool*);
void getBlowup(size_t size, size_t classSize, bool*);
void getClassSizes ();
void getMappingsUsage(size_t size, uint64_t address, size_t classSize);
void getMemUsageStart ();
void getMemUsageEnd ();
void getMetadata(size_t classSize);
void getMmapThreshold ();
void getOverhead(size_t size, uint64_t address, size_t classSize, bool*);
void getPerfInfo(PerfReadInfo*);
void globalizeThreadAllocData();
void increaseMemoryHWM(size_t size);
void myFree (void* ptr);
void* myMalloc (size_t size);
void readAllocatorFile();
void writeAllocData ();
void writeContention ();
void writeMappings();
void writeOverhead();
void writeThreadContention();
void writeThreadMaps();
void writeAddressUsage ();
unsigned search_vpage (uintptr_t vpage);
FreeObject* newFreeObject (uint64_t addr, uint64_t size);
LC* newLC ();
MmapTuple* newMmapTuple (uint64_t address, size_t length, int prot, char origin);
ObjectTuple* newObjectTuple (uint64_t address, size_t size);
Overhead* newOverhead();
ThreadContention* newThreadContention (uint64_t);
thread_alloc_data* newTad();
allocation_metadata init_allocation(size_t sz, enum memAllocType type);

#endif /* end of include guard: __LIBMALLOCPROF_H__ */
