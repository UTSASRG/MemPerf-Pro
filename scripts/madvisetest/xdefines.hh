#ifndef __XDEFINES_HH__
#define __XDEFINES_HH__

#include <stddef.h>
#include <stdint.h>
#include <ucontext.h>
#include <assert.h>

#include "slist.h"

extern char * getThreadBuffer();

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

inline unsigned long long rdtsc() {
    unsigned int lo, hi;
    asm volatile (
            "rdtsc"
            : "=a"(lo), "=d"(hi) /* outputs */
            : "a"(0)             /* inputs */
            : "%ebx", "%ecx");     /* clobbers*/
    unsigned long long retval = ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
    return retval;
}

extern "C" {
#ifndef CUSTOMIZED_STACK
__thread extern int _threadIndex;
#endif
typedef void * threadFunction(void*);

#ifdef LOGTOFILE
extern int outputfd;
#endif

#ifndef PTHREADEXIT_CODE
#define PTHREADEXIT_CODE 2230
#endif

typedef enum { LEFT, RIGHT } direction;

#ifdef LOGTOFILE
inline int getOutputFD() {
  return outputfd;
}
#endif

#ifndef CUSTOMIZED_STACK
inline int getThreadIndex() {
  return _threadIndex;
}

inline void setThreadIndex(int index) {
  _threadIndex = index;
}
#endif
inline size_t alignup(size_t size, size_t alignto) {
  return (size % alignto == 0) ? size : ((size + (alignto - 1)) & ~(alignto - 1));
}

inline void * alignupPointer(void * ptr, size_t alignto) {
  return ((intptr_t)ptr%alignto == 0) ? ptr : (void *)(((intptr_t)ptr + (alignto - 1)) & ~(alignto - 1));
}

inline size_t aligndown(size_t addr, size_t alignto) { return (addr & ~(alignto - 1)); }

#ifdef LOGTOFILE
#define OUTFD getOutputFD()
#else 
#define OUTFD 2
#endif
#define LOG_SIZE 4096

}; // extern "C"

#define LOG2(x) ((unsigned) (8*sizeof(unsigned long long) - __builtin_clzll((x)) - 1))

#define MAX_ALIVE_THREADS 128

#define PAGESIZE 0x1000
#define CACHE_LINE_SIZE 64

#define TWO_KILOBYTES 2048
#define PageSize 4096UL
#define PageMask (PageSize - 1)
#define PageSizeShiftBits 12
#define THREAD_MAP_SIZE	1280

#ifdef CUSTOMIZED_STACK
#define STACK_SIZE  		0x800000	// 8M, PageSize * N
#define STACK_SIZE_BIT  23	// 8M
#define MAX_THREADS 		MAX_ALIVE_THREADS
#define INLINE      		inline __attribute__((always_inline))

#define GUARD_PAGE_SIZE PageSize // PageSize * N
#include <sys/mman.h>
extern intptr_t globalStackAddr;
// Get the thread index by its stack address
INLINE int getThreadIndex(void* stackVar) {
		//int index = ((intptr_t)stackVar - globalStackAddr) / STACK_SIZE;
		int index = ((intptr_t)stackVar - globalStackAddr) >> STACK_SIZE_BIT;
		#ifdef CUSTOMIZED_MAIN_STACK
		assert(index >= 0 && index < MAX_THREADS);
		return index;
		#endif
		if(index >= MAX_THREADS || index <= 0) {
				return 0;
		} else {
				return index;
		}
}
#endif

#endif
