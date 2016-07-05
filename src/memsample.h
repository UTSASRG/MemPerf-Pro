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
#include <tuple>
#include <unordered_map>
#include <queue>

#define MMAP_PAGES 33	// must be in the form of 2^N + 1
#define DATA_MMAP_PAGES (MMAP_PAGES - 1)
#define MAPSIZE (MMAP_PAGES * getpagesize())
#define DATA_MAPSIZE (DATA_MMAP_PAGES * getpagesize())
#define OVERFLOW_INTERVAL 100
#define SHADOW_MEM_SIZE ONE_HUNDRED_MB
#define WORD_SIZE (sizeof(long))

// This value is chosen to ensure that all callsite ID's generated are
// greater than LOWEST_POS_CALLSITE_ID, and using a more obvious choice,
// such as 0x0, would not allow for this.
#define NO_CALLSITE 0xffffffffffffffff
#define LOWEST_POS_CALLSITE_ID 0x10000000100000
#define LOAD_ACCESS 0x1cd
#define STORE_ACCESS 0x2cd
#define MALLOC_HEADER_SIZE (sizeof(size_t))
#define FIVE_MB 5242880
#define ONE_HUNDRED_MB 104857600
#define MAX_FILENAME_LEN 128

typedef struct {
    void * ptr;
} freeReq;
typedef std::queue<freeReq> FreeQueue;

int initSampling(void);
void setupSampling(void);
void startSampling(void);
void stopSampling(void);
#endif
