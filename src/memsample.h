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

typedef struct {
	uint64_t addr;
	uint64_t numAccesses;
	uint64_t szTotal;
	uint64_t szFreed;
	uint64_t szUsed;
	uint32_t numAllocs;
	uint32_t numFrees;
} ObjectTuple;

typedef struct {

	uint64_t start;
	uint64_t end;
	size_t length;
	std::atomic_bool used;
} MmapTuple;

typedef struct {

	uint64_t VmSize_start;
	uint64_t VmSize_end;
	uint64_t VmPeak;
	uint64_t VmRSS_start;
	uint64_t VmRSS_end;
	uint64_t VmHWM;
	uint64_t VmLib;
} VmInfo;

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
	int perf_fd_tlb;
	int perf_fd_cache_miss;
    int perf_fd_cache_ref;
    int perf_fd_instr;
	pid_t tid;
} perf_info;

typedef struct {
    int64_t numFaults;
    int64_t numTlbMisses;
    int64_t numCacheMisses;
    int64_t numCacheRefs;
    int64_t numInstrs;
} thread_alloc_data;

void getPerfInfo(int64_t *, int64_t *, int64_t *, int64_t *, int64_t *);
int initSampling(void);
void setupSampling(void);
void doPerfRead(void);
void doPerfRead_noFile(void);
#endif
