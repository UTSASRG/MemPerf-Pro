#ifndef __LIBTESTPERF_H
#define __LIBTESTPERF_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <syscall.h>
#include <stdint.h>
#include <sys/mman.h>

#define TEMP_BUF_SIZE 10000000		// 10 MB
#define PRIV_ALLOC_SIZE 1000000000l	// 1 GB
#define MAX_FILENAME_LEN 128

pid_t gettid() {
    return syscall(__NR_gettid);
}

typedef struct {
    // starting address of this memory region
    void * privAllocBegin;
    // ending address of this memory region
    void * privAllocEnd;
    // current head pointer for this memory region. Used to
    // track the next available unit of memory in this region
    void * privAllocHead;
    // high-order four bytes of all memory locations located
    // within each memory region
    uint64_t highWord;
} allocData;

#endif
