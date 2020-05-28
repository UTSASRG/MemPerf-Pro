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

struct PerfReadInfo{
  uint64_t faults = 0;
  uint64_t tlb_read_misses = 0;
  uint64_t tlb_write_misses = 0;
  uint64_t cache_misses = 0;
  uint64_t instructions = 0;

  void add(struct PerfReadInfo newPerfReadInfo) {
      faults += newPerfReadInfo.faults;
      tlb_read_misses += newPerfReadInfo.tlb_read_misses;
      tlb_write_misses += newPerfReadInfo.tlb_write_misses;
      cache_misses += newPerfReadInfo.cache_misses;
      instructions += newPerfReadInfo.instructions;
  }
};

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

int initPMU(void);
void setupCounting(void);
void setupSampling(void);
void stopSampling(void);
void stopCounting(void);
void doSampleRead();
void getPerfCounts(PerfReadInfo*);
#endif
