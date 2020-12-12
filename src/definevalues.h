#ifndef MMPROF_DEFINEVALUES_H
#define MMPROF_DEFINEVALUES_H

#define OPEN_COUNTING_EVENT 1
#define OPEN_SAMPLING_EVENT 1
#define OPEN_SAMPLING_FOR_ALLOCS 1
#define RANDOM_PERIOD_FOR_ALLOCS 100
#define OPEN_BACKTRACE 1
#define PRINT_LEAK_OBJECTS 1

//#define OPEN_DEBUG 1
//#define OPEN_CPU_BINDING 1
//#define ENABLE_HP 1
//#define ENABLE_THP 1
//#define ENABLE_PRECISE_BLOWUP 1
//#define PRINT_MEM_DETAIL_THRESHOLD 10000

#define LOAD_ACCESS 0x81d0
#define STORE_ACCESS 0x82d0
#define LAST_USER_ADDR 0x7fffffffffff
#define EIGHT_BYTES 8
#define ONE_KB 1024l
#define ONE_MB 1048576l
#define ONE_GB 1073741824l
#define MAX_FILENAME_LEN 256

#define CACHELINE_SIZE 64
#define PAGESIZE 4096
#define PAGESIZE_HUGE 2*ONE_MB
#define SAMPLING_PERIOD 500000
//#define SAMPLING_PERIOD 200000
//#define SAMPLING_PERIOD 100000
//#define SAMPLING_PERIOD 10000
//#define SAMPLING_PERIOD 5000
//#define SAMPLING_PERIOD 1000
//#define SAMPLING_PERIOD 750
//#define SAMPLING_PERIOD 500
//#define SAMPLING_PERIOD 400
//#define SAMPLING_PERIOD 300
//#define SAMPLING_PERIOD 100
//#define MMAP_PAGES 257 // must be in the form of 2^N + 1
#define MMAP_PAGES 129 // must be in the form of 2^N + 1
#define DATA_MMAP_PAGES (MMAP_PAGES - 1)
#define MAPSIZE (MMAP_PAGES * PAGESIZE)
#define DATA_MAPSIZE (DATA_MMAP_PAGES * PAGESIZE)
#define OVERFLOW_INTERVAL 1


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

#define MAX_THREAD_NUMBER 1024

#define MAX_BT_ADDR_NUM 160

#define MAX_REGION_NUM 256

//#define LEAST_TOUCHING_THREADS 15

enum ObjectSizeType: unsigned char{
    SMALL,
    MEDIUM,
    LARGE,
    NUM_OF_OBJECTSIZETYPE
};

enum AllocationFunction: unsigned char{
    MALLOC,
    FREE,
    CALLOC,
    REALLOC,
    POSIX_MEMALIGN,
    MEMALIGN,
    NUM_OF_ALLOCATIONFUNCTION
};

enum AllocationTypeForOutputData: unsigned char{
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

enum LockTypes: unsigned char{
    MUTEX,
    SPIN,
    MUTEXTRY,
    SPINTRY,
    NUM_OF_LOCKTYPES
};

enum SystemCallTypes: unsigned char{
    MMAP,
    MADVISE,
    SBRK,
    MPROTECT,
    MUNMAP,
    MREMAP,
    NUM_OF_SYSTEMCALLTYPES
};

enum FalseSharingType: unsigned char {
    OBJECT,
    ACTIVE,
    PASSIVE,
    NUM_OF_FALSESHARINGTYPE
};

enum eMemAccessType: unsigned char{
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
