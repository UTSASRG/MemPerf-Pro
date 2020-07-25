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
#include <asm/unistd.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include "structs.h"
#include "spinlock.hh"
#include "shadowmemory.hh"


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
  int perf_fd;
  int perf_fd2;
  uint64_t prev_head;
	int perf_fd_fault;
	int perf_fd_tlb_reads;
	int perf_fd_tlb_writes;
	int perf_fd_cache_miss;
	int perf_fd_cache_ref;
	int perf_fd_instr;

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
