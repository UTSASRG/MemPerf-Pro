//
// Created by 86152 on 2020/5/20.
//

#ifndef MMPROF_DEFINEVALUES_H
#define MMPROF_DEFINEVALUES_H

#define CACHELINE_SIZE 64
#define PAGESIZE 4096
#define SAMPLING_PERIOD 10000
///#define MMAP_PAGES 33	// must be in the form of 2^N + 1
#define MMAP_PAGES 129
#define DATA_MMAP_PAGES (MMAP_PAGES - 1)
#define MAPSIZE (MMAP_PAGES * getpagesize())
#define DATA_MAPSIZE (DATA_MMAP_PAGES * getpagesize())
#define OVERFLOW_INTERVAL 1
#define SHADOW_MEM_SIZE (16 * ONE_GB)
#define WORD_SIZE (sizeof(void *))

//#define LOAD_ACCESS 0x1cd
//#define STORE_ACCESS 0x2cd
#define LOAD_ACCESS 0x81d0
#define STORE_ACCESS 0x82d0
#define LAST_USER_ADDR 0x7fffffffffff
#define MALLOC_HEADER_SIZE (sizeof(size_t))
#define EIGHT_BYTES 8
#define EIGHT_MB 8388608
#define ONE_HUNDRED_MB 104857600l
#define FIVE_HUNDRED_MB 524288000l
#define EIGHT_HUNDRED_MB 838860800l
#define ONE_KB 1024l
#define ONE_MB 1048576l
#define ONE_GB 1073741824l
#define TEN_GB 10737418240l
#define MAX_FILENAME_LEN 128
#define TEMP_BUF_SIZE EIGHT_MB



#define MAX(a,b) \
    ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#define MIN(a,b) \
    ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define CALLSITE_MAXIMUM_LENGTH 20
#define ENTRY_SIZE 8

#define PAGE_BITS 12
#define MY_METADATA_SIZE 4

#define ABNORMAL_VALUE_FOR_COUNTING_EVENTS 100000000l

#endif //MMPROF_DEFINEVALUES_H
