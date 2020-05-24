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
#include "definevalues.h"

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
    unsigned long numCacheWrites;
    //unsigned long numCacheOwnerConflicts;
    unsigned long numCacheBytes;
    unsigned long numPageBytes;
    unsigned long numObjectFS;
    unsigned long numActiveFS;
    unsigned long numPassiveFS;
    long numObjectFSCacheLine;
    long numActiveFSCacheLine;
    long numPassiveFSCacheLine;
    unsigned long cachelines;
} friendly_data;

typedef struct {
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

//typedef struct {
//    uint64_t cache_misses = 0;
//} CacheMissesOutsideInfo;

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
	//int perf_fd_cache_miss_outside;
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

    uint64_t numAllocationFaults;
    uint64_t numDeallocationFaults;

    uint64_t numAllocationTlbReadMisses;
    uint64_t numAllocationTlbWriteMisses;

    uint64_t numDeallocationTlbReadMisses;
    uint64_t numDeallocationTlbWriteMisses;

    uint64_t numAllocationCacheMisses;
    uint64_t numDeallocationCacheMisses;

    uint64_t numAllocationInstrs;
    uint64_t numDeallocationInstrs;

    uint64_t numAllocationFaultsFFL;

    uint64_t numAllocationTlbReadMissesFFL;
    uint64_t numAllocationTlbWriteMissesFFL;

    uint64_t numAllocationCacheMissesFFL;
    uint64_t numAllocationInstrsFFL;

		uint threads;
		//size_t blowup_bytes;

//		uint num_sbrk;
//		uint num_madvise;
//		uint malloc_mmaps;

		//uint size_sbrk;
		//uint blowup_allocations;

		uint64_t cycles_alloc;
		uint64_t cycles_allocFFL;
		uint64_t cycles_free;

		uint64_t numOutsideCacheMisses;
    uint64_t numOutsideFaults;
    uint64_t numOutsideTlbReadMisses;
    uint64_t numOutsideTlbWriteMisses;
    uint64_t numOutsideCycles;

    ulong numAllocs_large;
    ulong numFrees_large;

    uint64_t numAllocationFaults_large;
    uint64_t numDeallocationFaults_large;

    uint64_t numAllocationTlbReadMisses_large;
    uint64_t numAllocationTlbWriteMisses_large;

    uint64_t numDeallocationTlbReadMisses_large;
    uint64_t numDeallocationTlbWriteMisses_large;

    uint64_t numAllocationCacheMisses_large;
    uint64_t numDeallocationCacheMisses_large;

    uint64_t numAllocationInstrs_large;
    uint64_t numDeallocationInstrs_large;

    uint64_t cycles_alloc_large;
    uint64_t cycles_free_large;

		uint lock_nums[4];

} __attribute__((__aligned__(CACHELINE_SIZE))) thread_alloc_data;

int initPMU(void);
void setupCounting(void);
void setupSampling(void);
void stopSampling(void);
void stopCounting(void);
//void doPerfCounterRead(void);
void doSampleRead();
#endif
