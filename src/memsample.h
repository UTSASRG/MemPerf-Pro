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
#define WORD_SIZE (sizeof(void *))

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

    uint64_t numAllocationFaults;
    uint64_t numDeallocationFaults;

    uint64_t numAllocationTlbReadMisses;
    uint64_t numAllocationTlbWriteMisses;

    uint64_t numDeallocationTlbReadMisses;
    uint64_t numDeallocationTlbWriteMisses;

    uint64_t numAllocationCacheMisses;
    uint64_t numDeallocationCacheMisses;

    uint64_t numAllocationCacheRefs;
    uint64_t numDeallocationCacheRefs;

    uint64_t numAllocationInstrs;
    uint64_t numDeallocationInstrs;

    uint64_t numAllocationFaultsFFL;

    uint64_t numAllocationTlbReadMissesFFL;
    uint64_t numAllocationTlbWriteMissesFFL;

    uint64_t numAllocationCacheMissesFFL;
    uint64_t numAllocationCacheRefsFFL;
    uint64_t numAllocationInstrsFFL;

	uint threads;
	uint num_pthread_mutex_locks;
	uint num_trylock;
	uint64_t total_time_wait;
	size_t blowup_bytes;

	uint num_sbrk;
	uint num_madvise;
	uint malloc_mmaps;
	uint total_mmaps;

	uint size_sbrk;
	uint blowup_allocations;

	uint64_t cycles_free;
	uint64_t cycles_new;
	uint64_t cycles_reused;

} thread_alloc_data;

int initSampling(void);
void setupSampling(void);
void doPerfRead(void);
#endif
