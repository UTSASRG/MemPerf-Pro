#ifndef __MEMSAMPLE_H
#define __MEMSAMPLE_H

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syscall.h>
#include <sys/mman.h>
#include <atomic>

#define MMAP_PAGES 33	// must be in the form of 2^N + 1
#define DATA_MMAP_PAGES (MMAP_PAGES - 1)
#define MAPSIZE (MMAP_PAGES * getpagesize())
#define DATA_MAPSIZE (DATA_MMAP_PAGES * getpagesize())
#define OVERFLOW_INTERVAL 100
#define SHADOW_MEM_SIZE (16 * ONE_GB)
#define WORD_SIZE (sizeof(long))

// This value is chosen to ensure that all callsite ID's generated are
// greater than LOWEST_POS_CALLSITE_ID, and using a more obvious choice,
// such as 0x0, would not allow for this.
#define NO_CALLSITE 0xffffffffffffffff
#define LOAD_ACCESS 0x1cd
#define STORE_ACCESS 0x2cd
//#define LOAD_ACCESS 0x81d0
//#define STORE_ACCESS 0x82d0
#define MALLOC_HEADER_SIZE (sizeof(size_t))
#define EIGHT_BYTES 8
#define EIGHT_MB 8388608
#define ONE_HUNDRED_MB 104857600l
#define FIVE_HUNDRED_MB 524288000l
#define EIGHT_HUNDRED_MB 838860800l
#define ONE_GB 1073741824l
#define TEN_GB 10737418240
#define MAX_FILENAME_LEN 128
#define TEMP_BUF_SIZE EIGHT_MB

inline unsigned long long rdtscp() {
		unsigned int lo, hi;
		asm volatile (
						"rdtscp"
						: "=a"(lo), "=d"(hi) /* outputs */
						: "a"(0)             /* inputs */
						: "%ebx", "%ecx");     /* clobbers*/
		unsigned long long retval = ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
		return retval;
}

typedef struct {
    void * callsite1;
    void * callsite2;
    int numAllocs;
    int numFrees;
    long szFreed;
    long szTotal;
    long szUsed;
    long numAccesses;
} Tuple;

typedef struct addr2line_info {
    char exename[15];
    unsigned int lineNum;
} addrinfo;

typedef struct {
	char * stackStart = NULL;
	char * stackEnd = NULL;
	void * maxObjAddr = (void *)0x0;
	FILE * output = NULL;
	pid_t tid = 0;
} thread_data;

typedef struct {
	int perf_fd_fault;
	int perf_fd_tlb_reads;
	int perf_fd_tlb_writes;
	int perf_fd_cache_miss;
    int perf_fd_cache_ref;
    int perf_fd_instr;
	pid_t tid;
} perf_info;

typedef struct {            //struct for holding data about allocations
    uint64_t numMallocs;     //includes data from PMU and counts for types of alloc calls
    uint64_t numReallocs;    //FFL = from free list, data for allocations that came from freed objects
    uint64_t numCallocs;
    uint64_t numFrees;
    uint64_t numMallocsFFL;
    uint64_t numReallocsFFL;
    uint64_t numCallocsFFL;
    uint64_t numMallocFaults;
    uint64_t numReallocFaults;
    uint64_t numCallocFaults;
    uint64_t numFreeFaults;
    uint64_t numMallocTlbReadMisses;
    uint64_t numMallocTlbWriteMisses;
    uint64_t numReallocTlbReadMisses;
    uint64_t numReallocTlbWriteMisses;
    uint64_t numCallocTlbReadMisses;
    uint64_t numCallocTlbWriteMisses;
    /* NOTE(Stefen): Do we need to track free misses? */
    uint64_t numFreeTlbMisses;
    uint64_t numMallocCacheMisses;
    uint64_t numReallocCacheMisses;
    uint64_t numCallocCacheMisses;
    uint64_t numFreeCacheMisses;
    uint64_t numMallocCacheRefs;
    uint64_t numReallocCacheRefs;
    uint64_t numCallocCacheRefs;
    uint64_t numFreeCacheRefs;
    uint64_t numMallocInstrs;
    uint64_t numReallocInstrs;
    uint64_t numCallocInstrs;
    uint64_t numFreeInstrs;
    uint64_t numMallocFaultsFFL;
    uint64_t numReallocFaultsFFL;
    uint64_t numCallocFaultsFFL;
    uint64_t numMallocTlbReadMissesFFL;
    uint64_t numMallocTlbWriteMissesFFL;
    uint64_t numReallocTlbReadMissesFFL;
    uint64_t numReallocTlbWriteMissesFFL;
    uint64_t numCallocTlbReadMissesFFL;
    uint64_t numCallocTlbWriteMissesFFL;
    uint64_t numMallocCacheMissesFFL;
    uint64_t numReallocCacheMissesFFL;
    uint64_t numCallocCacheMissesFFL;
    uint64_t numMallocCacheRefsFFL;
    uint64_t numReallocCacheRefsFFL;
    uint64_t numCallocCacheRefsFFL;
    uint64_t numMallocInstrsFFL;
    uint64_t numReallocInstrsFFL;
    uint64_t numCallocInstrsFFL;
} thread_alloc_data;

int initSampling(void);
void setupSampling(void);
void doPerfRead(void);
#endif
