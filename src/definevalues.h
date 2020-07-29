//
// Created by 86152 on 2020/5/20.
//

#ifndef MMPROF_DEFINEVALUES_H
#define MMPROF_DEFINEVALUES_H

#define CACHELINE_SIZE 64
#define PAGESIZE 4096
#define SAMPLING_PERIOD 10000
#define MMAP_PAGES 129 // must be in the form of 2^N + 1
#define DATA_MMAP_PAGES (MMAP_PAGES - 1)
#define MAPSIZE (MMAP_PAGES * getpagesize())
#define DATA_MAPSIZE (DATA_MMAP_PAGES * getpagesize())
#define OVERFLOW_INTERVAL 1

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
#define MAX_FILENAME_LEN 256
#define TEMP_BUF_SIZE EIGHT_MB



#define MAX(a,b) \
    ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#define MIN(a,b) \
    ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define ENTRY_SIZE 8

#define PAGE_BITS 12
#define MY_METADATA_SIZE 4

#define ABNORMAL_VALUE 100000000l
#define MAX_OBJ_NUM 4194304
#define MAX_LOCK_NUM 512

#define MAX_THREAD_NUMBER 2048

#define RANDOM_PERIOD_FOR_COUNTING_EVENT 100

enum ObjectSizeType{
    SMALL,
    MEDIUM,
    LARGE,
    NUM_OF_OBJECTSIZETYPE
};

enum AllocationFunction{
    MALLOC,
    FREE,
    CALLOC,
    REALLOC,
    POSIX_MEMALIGN,
    MEMALIGN,
    NUM_OF_ALLOCATIONFUNCTION
};

enum AllocationTypeForOutputData{
    SERIAL_SMALL_NEW_MALLOC,
    SERIAL_MEDIUM_NEW_MALLOC,
    SERIAL_SMALL_REUSED_MALLOC,
    SERIAL_MEDIUM_REUSED_MALLOC,
    SERIAL_LARGE_MALLOC,

    SERIAL_SMALL_FREE,
    SERIAL_MEDIUM_FREE,
    SERIAL_LARGE_FREE,

    SERIAL_NORMAL_CALLOC,
    SERIAL_NORMAL_REALLOC,
    SERIAL_NORMAL_POSIX_MEMALIGN,
    SERIAL_NORMAL_MEMALIGN,

    PARALLEL_SMALL_NEW_MALLOC,
    PARALLEL_MEDIUM_NEW_MALLOC,
    PARALLEL_SMALL_REUSED_MALLOC,
    PARALLEL_MEDIUM_REUSED_MALLOC,
    PARALLEL_LARGE_MALLOC,

    PARALLEL_SMALL_FREE,
    PARALLEL_MEDIUM_FREE,
    PARALLEL_LARGE_FREE,

    PARALLEL_NORMAL_CALLOC,
    PARALLEL_NORMAL_REALLOC,
    PARALLEL_NORMAL_POSIX_MEMALIGN,
    PARALLEL_NORMAL_MEMALIGN,


    NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA
};

enum LockTypes{
    MUTEX,
    SPIN,
    MUTEXTRY,
    SPINTRY,
    NUM_OF_LOCKTYPES
};

enum SystemCallTypes{
    MMAP,
    MADVISE,
    SBRK,
    MPROTECT,
    MUNMAP,
    MREMAP,
    NUM_OF_SYSTEMCALLTYPES
};

enum FalseSharingType {
    OBJECT,
    ACTIVE,
    PASSIVE,
    NUM_OF_FALSESHARINGTYPE
};

enum eMemAccessType{
    E_MEM_NONE = 0,
    E_MEM_LOAD,
    E_MEM_STORE,
    E_MEM_PFETCH,
    E_MEM_EXEC,
    E_MEM_UNKNOWN,
};
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
#endif //MMPROF_DEFINEVALUES_H
