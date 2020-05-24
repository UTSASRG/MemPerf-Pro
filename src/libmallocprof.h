#ifndef __LIBMALLOCPROF_H__
#define __LIBMALLOCPROF_H__

#include "memsample.h"
#include <signal.h>
#include <limits.h>
#include "definevalues.h"

typedef enum {
		LOCK_TYPE_MUTEX,  // 0
		LOCK_TYPE_SPINLOCK, // 1
		LOCK_TYPE_TRYLOCK,  // 2
		LOCK_TYPE_SPIN_TRYLOCK, //3
		LOCK_TYPE_TOTAL // 4
} LockType;

#define MEM_SYS_START LOCK_TYPE_TOTAL
typedef enum {
  MEM_SYSCALL_MMAP = MEM_SYS_START,
  MEM_SYSCALL_SBRK, 
  MEM_SYSCALL_MADVISE,
  MEM_SYSCALL_MUNMAP,
  MEM_SYSCALL_MREMAP,
  MEM_SYSCALL_MPROTECT,
  MEM_SYSCALL_TOTAL
} MemSyscallType; 

typedef struct {
  ulong calls[4] = {0, 0, 0, 0};
  ulong cycles[4] = {0, 0, 0, 0};
  ulong new_calls = 0;
  ulong ffl_calls = 0;
  ulong new_cycles = 0;
  ulong ffl_cycles = 0;
} PerPrimitiveData;

//Structure for perthread contention
typedef struct {
	pid_t tid = 0;
 
  // Instead for different types. Let's use an array here. 
  PerPrimitiveData pmdata[LOCK_TYPE_TOTAL];

	ulong mmap_waits_alloc = 0;
    ulong mmap_waits_free = 0;
    ulong mmap_waits_alloc_large = 0;
    ulong mmap_waits_free_large = 0;

    ulong mmap_wait_cycles_alloc = 0;
    ulong mmap_wait_cycles_free = 0;
    ulong mmap_wait_cycles_alloc_large = 0;
    ulong mmap_wait_cycles_free_large = 0;

	ulong sbrk_waits_alloc = 0;
    ulong sbrk_waits_free = 0;
    ulong sbrk_waits_alloc_large = 0;
    ulong sbrk_waits_free_large = 0;

	ulong sbrk_wait_cycles_alloc = 0;
    ulong sbrk_wait_cycles_free = 0;
    ulong sbrk_wait_cycles_alloc_large = 0;
    ulong sbrk_wait_cycles_free_large = 0;

	ulong madvise_waits_alloc = 0;
    ulong madvise_waits_free = 0;
    ulong madvise_waits_alloc_large = 0;
    ulong madvise_waits_free_large = 0;

	ulong madvise_wait_cycles_alloc = 0;
    ulong madvise_wait_cycles_free = 0;
    ulong madvise_wait_cycles_alloc_large = 0;
    ulong madvise_wait_cycles_free_large = 0;

	ulong munmap_waits_alloc = 0;
    ulong munmap_waits_free = 0;
    ulong munmap_waits_alloc_large = 0;
    ulong munmap_waits_free_large = 0;

	ulong munmap_wait_cycles_alloc = 0;
    ulong munmap_wait_cycles_free = 0;
    ulong munmap_wait_cycles_alloc_large = 0;
    ulong munmap_wait_cycles_free_large = 0;

	ulong mremap_waits_alloc = 0;
    ulong mremap_waits_free = 0;
    ulong mremap_waits_alloc_large = 0;
    ulong mremap_waits_free_large = 0;

	ulong mremap_wait_cycles_alloc = 0;
    ulong mremap_wait_cycles_free = 0;
    ulong mremap_wait_cycles_alloc_large = 0;
    ulong mremap_wait_cycles_free_large = 0;

	ulong mprotect_waits_alloc = 0;
    ulong mprotect_waits_free = 0;
    ulong mprotect_waits_alloc_large = 0;
    ulong mprotect_waits_free_large = 0;

	ulong mprotect_wait_cycles_alloc = 0;
    ulong mprotect_wait_cycles_free = 0;
    ulong mprotect_wait_cycles_alloc_large = 0;
    ulong mprotect_wait_cycles_free_large = 0;


// TP END
	long realMemoryUsage = 0;
    long maxRealMemoryUsage = 0;
    long realAllocatedMemoryUsage = 0;
    long maxRealAllocatedMemoryUsage = 0;
    long totalMemoryUsage = 0;
    long maxTotalMemoryUsage = 0;

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
	unsigned long numAccesses = 0;
	unsigned long pageUtilTotal = 0;
	unsigned long cacheUtilTotal = 0;
} PerfAppFriendly;

struct stack_frame {
	struct stack_frame * prev;	// pointing to previous stack_frame
	void * caller_address;		// the address of caller
};


typedef struct {
	void* start;
	void* end;
	unsigned kb;
} SMapEntry;

inline bool isAllocatorInCallStack();
size_t getClassSizeFor(size_t size);
int num_used_pages(uintptr_t vstart, uintptr_t vend);
void incrementMemoryUsage(size_t size, size_t classSize, size_t new_touched_bytes, void * object);
void decrementMemoryUsage(size_t size, size_t classSize, void * addr);
void getAddressUsage(size_t size, uint64_t address, uint64_t cycles);

void getMappingsUsage(size_t size, uint64_t address, size_t classSize);
void getMetadata(size_t classSize);

void getPerfCounts(PerfReadInfo*);

void globalizeTAD();

void readAllocatorFile();
void writeAllocData ();
void writeContention ();
void writeMappings();

void writeThreadContention();
void writeThreadMaps();

ObjectTuple* newObjectTuple (uint64_t address, size_t size);

void initGlobalFreeArray();
void initLocalFreeArray();

void* myLocalMalloc(size_t);
void myLocalFree(void*);
void initGlobalCSM();
SMapEntry* newSMapEntry();

void updateGlobalFriendlinessData();
void calcAppFriendliness();
const char * LockTypeToString(LockType type);

inline double safeDivisor(ulong divisor) {
	return (!divisor) ? 1.0 : (double)divisor;
}

#include "shadowmemory.hh"
#endif /* end of include guard: __LIBMALLOCPROF_H__ */
