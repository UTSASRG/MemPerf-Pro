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
    int64_t numMallocs;     //includes data from PMU and counts for types of alloc calls
    int64_t numReallocs;    //FFL = from free list, data for allocations that came from freed objects
    int64_t numCallocs;
    int64_t numFrees;

    int64_t numMallocsFFL;
    int64_t numReallocsFFL;
    int64_t numCallocsFFL;

    int64_t numMallocFaults;
    int64_t numReallocFaults;
    int64_t numCallocFaults;
    int64_t numFreeFaults;

    int64_t numMallocTlbReadMisses;
    int64_t numMallocTlbWriteMisses;

    int64_t numReallocTlbReadMisses;
    int64_t numReallocTlbWriteMisses;

    int64_t numCallocTlbReadMisses;
    int64_t numCallocTlbWriteMisses;

    /* NOTE(Stefen): Do we need to track free misses? */
    int64_t numFreeTlbMisses;

    int64_t numMallocCacheMisses;
    int64_t numReallocCacheMisses;
    int64_t numCallocCacheMisses;
    int64_t numFreeCacheMisses;

    int64_t numMallocCacheRefs;
    int64_t numReallocCacheRefs;
    int64_t numCallocCacheRefs;
    int64_t numFreeCacheRefs;

    int64_t numMallocInstrs;
    int64_t numReallocInstrs;
    int64_t numCallocInstrs;
    int64_t numFreeInstrs;

    int64_t numMallocFaultsFFL;
    int64_t numReallocFaultsFFL;
    int64_t numCallocFaultsFFL;

    int64_t numMallocTlbReadMissesFFL;
    int64_t numMallocTlbWriteMissesFFL;

    int64_t numReallocTlbReadMissesFFL;
    int64_t numReallocTlbWriteMissesFFL;

    int64_t numCallocTlbReadMissesFFL;
    int64_t numCallocTlbWriteMissesFFL;

    int64_t numMallocCacheMissesFFL;
    int64_t numReallocCacheMissesFFL;
    int64_t numCallocCacheMissesFFL;

    int64_t numMallocCacheRefsFFL;
    int64_t numReallocCacheRefsFFL;
    int64_t numCallocCacheRefsFFL;

    int64_t numMallocInstrsFFL;
    int64_t numReallocInstrsFFL;
    int64_t numCallocInstrsFFL;
} thread_alloc_data;

int initSampling(void);
void setupSampling(void);
void doPerfRead(void);
#endif
