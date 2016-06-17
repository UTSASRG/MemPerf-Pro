#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Number of MMAP pages needs to be in the form 2^N + 1.
#define MMAP_PAGES 5
#define DATA_MMAP_PAGES (MMAP_PAGES - 1)
#define MAPSIZE (MMAP_PAGES * getpagesize())
#define DATA_MAPSIZE (DATA_MMAP_PAGES * getpagesize())
#define OVERFLOW_INTERVAL 20
#define SHADOW_MEM_SIZE (104857600)     // 100MB
#define WORD_SIZE (sizeof(long))
#define LOWEST_POS_CALLSITE_ID 0x10000000100000
#define LOAD_ACCESS 0x1cd
#define STORE_ACCESS 0x2cd

int initSampling(void);
void startSampling(void);
void stopSampling(void);
void setupSampling(void);
