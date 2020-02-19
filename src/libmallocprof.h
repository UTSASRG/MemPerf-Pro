#ifndef __LIBMALLOCPROF_H__
#define __LIBMALLOCPROF_H__

#include "memsample.h"
#include <signal.h>
#include <limits.h>

#define MAX(a,b) \
    ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#define MIN(a,b) \
    ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

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
#define MY_METADATA_SIZE 4
#define TEMP_MEM_SIZE 1024 * 1024 * 1024 //1GB
#define MAX_CLASS_SIZE 1050000
#define LOCAL_BUF_SIZE 204800000

pid_t gettid();

typedef enum {
		MUTEX,
		SPINLOCK,
		TRYLOCK,
		SPIN_TRYLOCK
} LockType;

//Structures
typedef struct {
	pid_t tid = 0;
	ulong mutex_waits = 0;
	ulong mutex_wait_cycles = 0;
	ulong spinlock_waits = 0;
	ulong spinlock_wait_cycles = 0;
	ulong mutex_trylock_waits = 0;
	ulong mutex_trylock_fails = 0;
	ulong spin_trylock_waits = 0;
	ulong spin_trylock_fails = 0;
	ulong mmap_waits = 0;
	ulong mmap_wait_cycles = 0;
	ulong sbrk_waits = 0;
	ulong sbrk_wait_cycles = 0;
	ulong madvise_waits = 0;
	ulong madvise_wait_cycles = 0;
	ulong munmap_waits = 0;
	ulong munmap_wait_cycles = 0;
	ulong mremap_waits = 0;
	ulong mremap_wait_cycles = 0;
	ulong mprotect_waits = 0;
	ulong mprotect_wait_cycles = 0;

	size_t realMemoryUsage = 0;
	size_t maxRealMemoryUsage = 0;
	size_t realAllocatedMemoryUsage = 0;
	size_t maxRealAllocatedMemoryUsage = 0;
	size_t totalMemoryUsage = 0;
	size_t maxTotalMemoryUsage = 0;

  ulong lock_counter = 0;
  uint64_t critical_section_start = 0;
  ulong critical_section_counter = 0;
  uint64_t critical_section_duration = 0;
} __attribute__((__aligned__(CACHELINE_SIZE))) ThreadContention;

typedef struct {
  long realMemoryUsage = 0;
	long realAllocatedMemoryUsage = 0;
	long freedTotalBytes = 0;
	long totalMemoryUsage = 0;
	long maxTotalMemoryUsage = 0;
} MemoryUsage;

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
	unsigned long numAccesses = 0;
	unsigned long pageUtilTotal = 0;
	unsigned long cacheUtilTotal = 0;
} PerfAppFriendly;

typedef struct LockContention {
	std::atomic<int> contention;
	std::atomic<int> maxContention;
	LockType lockType;
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
	short classSizeIndex;
	uint64_t cycles;
	uint64_t address;
	uint64_t tsc_before;
	uint64_t tsc_after;
	enum memAllocType type;
	thread_alloc_data *tad;
} allocation_metadata;

typedef struct {
	void* start;
	void* end;
	unsigned kb;
} SMapEntry;

typedef struct {
	uint64_t kb;
	uint64_t alignment;
	uint64_t blowup;
	float efficiency;
} OverheadSample;

// Functions 
#ifdef MAPPINGS
bool mappingEditor (void* addr, size_t len, int prot);
#endif
inline bool isAllocatorInCallStack();
size_t getClassSizeFor(size_t size);
int num_used_pages(uintptr_t vstart, uintptr_t vend);
void analyzePerfInfo(allocation_metadata *metadata);
void analyzeAllocation(allocation_metadata *metadata);
void calculateMemOverhead();
void doBefore(allocation_metadata *metadata);
void doAfter(allocation_metadata *metadata);
void incrementMemoryUsage(size_t size, size_t new_touched_bytes, void * object);
void decrementMemoryUsage(void* addr);
void getAddressUsage(size_t size, uint64_t address, uint64_t cycles);
void getAlignment(size_t, size_t);
void getBlowup(size_t size, size_t classSize, short class_size_index, bool*);
void getMappingsUsage(size_t size, uint64_t address, size_t classSize);
void getMetadata(size_t classSize);
void getOverhead(size_t size, uint64_t address, size_t classSize, short classSizeIndex, bool*);
void getPerfCounts(PerfReadInfo*, bool enableCounters);
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
LC* newLC(LockType lockType, int contention = 1);
MmapTuple* newMmapTuple (uint64_t address, size_t length, int prot, char origin);
ObjectTuple* newObjectTuple (uint64_t address, size_t size);
Overhead* newOverhead();
allocation_metadata init_allocation(size_t sz, enum memAllocType type);
size_t updateFreeCounters(void * address);
short getClassSizeIndex(size_t size);
void initGlobalFreeArray();
void initLocalFreeArray();
void initLocalNumAllocsBySizes ();
void initGlobalnumAllocsBySizes ();
void initLocalNumAllocsFFLBySizes ();
void initGlobalnumAllocsFFLBySizes ();
void initMyLocalMem();
void* myLocalMalloc(size_t);
void myLocalFree(void*);
void printMyMemUtilization();
void initGlobalCSM();
SMapEntry* newSMapEntry();
void start_smaps();
void sampleMemoryOverhead(int, siginfo_t*, void*);
void updateGlobalFriendlinessData();
void calcAppFriendliness();
const char * LockTypeToString(LockType type);

inline double safeDivisor(ulong divisor) {
	return (!divisor) ? 1.0 : (double)divisor;
}

#include "shadowmemory.hh"
#endif /* end of include guard: __LIBMALLOCPROF_H__ */
