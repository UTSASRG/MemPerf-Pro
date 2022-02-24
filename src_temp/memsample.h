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
#include "allocatingstatus.h"

typedef struct {
//    bool samplesLost;
    int perf_fd;
    int perf_fd2;

#ifdef COUNTING
    int perf_fd_fault;
    int perf_fd_l1cache_load;
    int perf_fd_l1cache_load_miss;
//    int perf_fd_llc_load;
//    int perf_fd_llc_load_miss;
	int perf_fd_instr;
#endif

    uint64_t prev_head;
    char * data_buf_copy = NULL;
    void * ring_buf = NULL;
    void * ring_buf_data_start = NULL;
    void * aux_buf = NULL;
    pid_t tid;
} perf_info;

#ifdef OPEN_SAMPLING_EVENT
void initPMU(void);
void initPMU2(void);
void sampleHandler(int signum, siginfo_t *info, void *p);
void setupSampling(void);
void stopSampling(void);
void pauseSampling(void);
void restartSampling(void);
#ifdef COUNTING
void getPerfCounts (PerfReadInfo * i);
void setupCounting(void);
void stopCounting(void);
#endif
//void doSampleRead();
#endif

#endif
