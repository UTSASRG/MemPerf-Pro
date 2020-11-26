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
    bool samplesLost;
    bool initialized;
    int perf_fd;
    int perf_fd2;
    int perf_fd_fault;
    int perf_fd_cache;
    int perf_fd_instr;

    uint64_t prev_head;
    char * data_buf_copy = NULL;
    void * ring_buf = NULL;
    void * ring_buf_data_start = NULL;
    void * aux_buf = NULL;
    pid_t tid;
} perf_info;


void initPMU(void);
void setupCounting(void);
void setupSampling(void);
void stopSampling(void);
void stopCounting(void);
void doSampleRead();
void getPerfCounts(PerfReadInfo*);
#endif
