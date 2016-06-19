#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MMAP_PAGES 33	// must be in the form of 2^N + 1
#define DATA_MMAP_PAGES (MMAP_PAGES - 1)
#define MAPSIZE (MMAP_PAGES * getpagesize())
#define DATA_MAPSIZE (DATA_MMAP_PAGES * getpagesize())
#define OVERFLOW_INTERVAL 100
#define SHADOW_MEM_SIZE ONE_HUNDRED_MB
#define WORD_SIZE (sizeof(long))
#define LOWEST_POS_CALLSITE_ID 0x10000000100000
#define LOAD_ACCESS 0x1cd
#define STORE_ACCESS 0x2cd
#define MALLOC_HEADER_SIZE (sizeof(size_t))
#define FIVE_MB 5242880
#define ONE_HUNDRED_MB 104857600

int initSampling(void);
void setupSampling(void);
void startSampling(void);
void stopSampling(void);
