#if !defined(DOUBLETAKE_XDEFINES_H)
#define DOUBLETAKE_XDEFINES_H

#include <stddef.h>
#include <ucontext.h>

#include "list.hh"

/*
 * @file   xdefines.h
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */
extern "C" {
	inline size_t alignup(size_t size, size_t alignto) {
  	return ((size + (alignto - 1)) & ~(alignto - 1));
	}

	inline int gettid(void) {
		return syscall(SYS_gettid());
	}
  
	typedef void * threadFunction (void *);
	typedef struct thread {
    // The heap index and thread index to the threads pool.
    int       index;
    int       heapid;

    // What is the actual thread id. tid can be greater than 1024.
    int       tid;

    pthread_t self; // Results of pthread_self
		void * stackTop; // Used to calculate stack offset
		
		// The following is the parameter about starting function. 
    threadFunction * startRoutine;
    void * startArg;
	} thread_t;

extern __thread thread_t * current;
inline size_t aligndown(size_t addr, size_t alignto) { return (addr & ~(alignto - 1)); }

	
};
#define WP_START_OFFSET sizeof(unsigned long)

class xdefines {
public:
  enum { USER_HEAP_SIZE = 1048576UL * 8192 }; // 8G
  enum { USER_HEAP_BASE = 0x100000000 }; // 4G
  enum { MAX_USER_SPACE = USER_HEAP_BASE + USER_HEAP_SIZE };

  // 128M so that almost all memory is allocated from the begining.
  enum { USER_HEAP_CHUNK = 1048576 * 4 };
  enum { INTERNAL_HEAP_CHUNK = 1048576 };
  enum { OBJECT_SIZE_BASE = 16 };
  
  enum { MAX_CPU_NUMBER = 32 };
  enum { PageSize = 4096UL };
  enum { PAGE_SIZE_MASK = (PageSize - 1) };

  enum {MAX_THREADS = 4096 }; 
  enum { MAX_ALIVE_THREADS = 128 };

	// Start to reap threads when reaplable threas is larer than that.
  enum { MAX_REAPABLE_THREADS = (MAX_ALIVE_THREADS - 10) };
  enum { NUM_HEAPS = MAX_ALIVE_THREADS };
  enum { SYNCMAP_SIZE = 4096 };
  enum { THREAD_MAP_SIZE = 1024 };
  enum { MAX_STACK_SIZE = 0xa00000UL };  // 64pages
  enum { TEMP_STACK_SIZE = 0xa00000UL }; // 64 pages
  enum { NUM_GLOBALS = 128 }; // At least, we need app globals, libc globals and libthread globals.
  // enum { MAX_GLOBALS_SIZE = 1048576UL * 10 };
  enum { CACHE_LINE_SIZE = 64 };

  /**
   * Definition of sentinel information.
   */
  enum { WORD_SIZE = sizeof(size_t) };
  enum { WORD_SIZE_MASK = WORD_SIZE - 1 };
  enum { SENTINEL_SIZE = WORD_SIZE };
#ifdef X86_32BIT
  enum { SENTINEL_WORD = 0xCAFEBABE };
  enum { MEMALIGN_SENTINEL_WORD = 0xDADEBABE };
#else
  enum { SENTINEL_WORD = 0xCAFEBABECAFEBABE };
  enum { MEMALIGN_SENTINEL_WORD = 0xDADEBABEDADEBABE };
#endif
};

#endif
