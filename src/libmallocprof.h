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
#define BUMP_POINTER_KEY 0

//Bump-pointer key to overhead hashmap
#define BP_OVERHEAD 0

#define PAGE_BITS 12 
#define TEMP_MEM_SIZE 1024 * 1024 * 1024 //1GB
#define MAX_CLASS_SIZE 1050000
#define LOCAL_BUF_SIZE 204800000

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
	uint64_t munmap_waits = 0;
	uint64_t munmap_wait_cycles = 0;
	uint64_t mremap_waits = 0;
	uint64_t mremap_wait_cycles = 0;
	uint64_t mprotect_waits = 0;
	uint64_t mprotect_wait_cycles = 0;

  uint64_t realMemoryUsage = 0;
  uint64_t totalMemoryUsage = 0;
  
  uint64_t lock_counter = 0;
  uint64_t critical_section_start = 0;
  uint64_t critical_section_duration = 0;
} __attribute__((__aligned__(64))) ThreadContention;

typedef struct {
	unsigned szUsed;
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
	uint64_t faults = 0;
	uint64_t tlb_read_misses = 0;
	uint64_t tlb_write_misses = 0;
	uint64_t cache_misses = 0;
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
#ifdef MAPPINGS
bool mappingEditor (void* addr, size_t len, int prot);
#endif
inline bool isAllocatorInCallStack();
size_t getClassSizeFor(size_t size);
int num_used_pages(uintptr_t vstart, uintptr_t vend);
void analyzePerfInfo(allocation_metadata *metadata);
void analyzeAllocation(allocation_metadata *metadata);
void calculateMemOverhead ();
void doBefore(allocation_metadata *metadata);
void doAfter(allocation_metadata *metadata);
void incrementMemoryUsage(size_t size);
void decrementMemoryUsage(void* addr);
void getAddressUsage(size_t size, uint64_t address, uint64_t cycles);
void getAlignment(size_t size, size_t classSize);
void getBlowup(size_t size, size_t classSize, bool*);
void getMappingsUsage(size_t size, uint64_t address, size_t classSize);
void getMetadata(size_t classSize);
void getOverhead(size_t size, uint64_t address, size_t classSize, bool*);
void getPerfInfo(PerfReadInfo*);
void globalizeTAD();
void myFree (void* ptr);
void* myMalloc (size_t size);
void readAllocatorFile();
void writeAllocData ();
void writeContention ();
void writeMappings();
void writeOverhead();
void writeThreadContention();
void writeThreadMaps();
LC* newLC ();
MmapTuple* newMmapTuple (uint64_t address, size_t length, int prot, char origin);
ObjectTuple* newObjectTuple (uint64_t address, size_t size);
Overhead* newOverhead();
ThreadContention* newThreadContention (uint64_t);
thread_alloc_data* newTad();
allocation_metadata init_allocation(size_t sz, enum memAllocType type);
size_t updateFreeCounters(uint64_t address);
short getClassSizeIndex(size_t size);
void initGlobalFreeArray();
void initLocalFreeArray();
void initMyLocalMem();
void* myLocalMalloc(size_t);
void myLocalFree(void*);
void printMyMemUtilization();
void initGlobalCSM();
void * myTreeMalloc(struct libavl_allocator * allocator, size_t size);
void myTreeFree(struct libavl_allocator * allocator, void * block);
int compare_ptr(const void *rb_a, const void *rb_b, void *rb_param);

#endif /* end of include guard: __LIBMALLOCPROF_H__ */
