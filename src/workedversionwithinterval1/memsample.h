#ifndef __MEMSAMPLE_H
#define __MEMSAMPLE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <syscall.h>
#include <sys/mman.h>

#define MMAP_PAGES 4097	// must be in the form of 2^N + 1
#define DATA_MMAP_PAGES (MMAP_PAGES - 1)
#define MAPSIZE (MMAP_PAGES * getpagesize())
#define DATA_MAPSIZE (DATA_MMAP_PAGES * getpagesize())

#define OVERFLOW_INTERVAL 1
//0000
//#define OVERFLOW_INTERVAL 0x10000000
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

typedef void * QueueItem;
typedef struct structQueueNode {
	structQueueNode * next;
	QueueItem item;
} QueueNode;
typedef struct {
	QueueNode * head;
	QueueNode * tail;
} FreeQueue;

typedef struct addr2line_info {
    char exename[15];
    unsigned int lineNum;
} addrinfo;

int initSampling(void);
void setupSampling(void);
void startSampling(void);
void stopSampling(void);
#endif
