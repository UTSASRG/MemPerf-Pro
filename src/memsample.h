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

#define CACHELINE_SIZE 64
#define PAGESIZE 4096
#define SAMPLING_PERIOD 500
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
#define LAST_USER_ADDR 0x7fffffffffff
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

typedef enum {
  E_MEM_NONE = 0,
  E_MEM_LOAD,
  E_MEM_STORE,
  E_MEM_PFETCH,
  E_MEM_EXEC,
  E_MEM_UNKNOWN,
} eMemAccessType;

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
    unsigned long numAccesses;
    unsigned long numCacheOwnerConflicts;
    unsigned long numCacheBytes;
    unsigned long numPageBytes;
} friendly_data;

typedef struct {
	char * stackStart = NULL;
	char * stackEnd = NULL;
	FILE * output = NULL;
	pid_t tid = 0;
	friendly_data friendlyData;
} thread_data;

typedef struct {
  uint64_t faults = 0;
  uint64_t tlb_read_misses = 0;
  uint64_t tlb_write_misses = 0;
  uint64_t cache_misses = 0;
  uint64_t instructions = 0;
} PerfReadInfo;

typedef struct {
  int perf_fd;
  int perf_fd2;
  uint64_t prev_head;
	int perf_fd_fault;
	int perf_fd_tlb_reads;
	int perf_fd_tlb_writes;
	int perf_fd_cache_miss;
	int perf_fd_cache_ref;
	int perf_fd_instr;
	// Discontiguous sample data from the perf ring buffer will be copied into
	// data_buf_copy in the correct order (that is, eliminating the discontinuity
  // present in the ring buffer (ring_buf)).
  char * data_buf_copy = NULL;
  void * ring_buf = NULL;
  void * ring_buf_data_start = NULL;
  void * aux_buf = NULL;
  bool samplesLost;
  long numSampleReadOps;
  long numSamples;
  long numSignalsRecvd;
  long numSampleHits;
  uint64_t time_zero;
  uint64_t time_mult;
  uint64_t time_shift;
  bool initialized;
	pid_t tid;
} perf_info;

typedef struct {            //struct for holding data about allocations

		ulong numAllocs;
		ulong numAllocsFFL;
		ulong numFrees;

		long numAllocationFaults;
		long numDeallocationFaults;

		long numAllocationTlbReadMisses;
		long numAllocationTlbWriteMisses;

		long numDeallocationTlbReadMisses;
		long numDeallocationTlbWriteMisses;

		long numAllocationCacheMisses;
		long numDeallocationCacheMisses;

		long numAllocationInstrs;
		long numDeallocationInstrs;

		long numAllocationFaultsFFL;

		long numAllocationTlbReadMissesFFL;
		long numAllocationTlbWriteMissesFFL;

		long numAllocationCacheMissesFFL;
		long numAllocationInstrsFFL;

		uint threads;
		uint num_pthread_mutex_locks;
		uint num_trylock;
		uint64_t total_time_wait;
		size_t blowup_bytes;

		uint num_sbrk;
		uint num_madvise;
		uint malloc_mmaps;

		uint size_sbrk;
		uint blowup_allocations;

		uint64_t cycles_alloc;
		uint64_t cycles_allocFFL;
		uint64_t cycles_free;

} __attribute__((__aligned__(CACHELINE_SIZE))) thread_alloc_data;

int initPMU(void);
void setupCounting(void);
void setupSampling(void);
void stopSampling(void);
void stopCounting(void);
void doPerfCounterRead(void);
void doSampleRead();
#endif
