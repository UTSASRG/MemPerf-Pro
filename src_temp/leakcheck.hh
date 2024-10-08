#if !defined(DOUBLETAKE_LEAKCHECK_H)
#define DOUBLETAKE_LEAKCHECK_H

/*
 * @file   leakcheck.h
 * @brief  Detecting leakage usage case.
           Basic idea:
           We first traverse the heap to get an alive list (not freed objects) and verify whether
           these objects are reachable from stack, registers and global variables or not.
           If an object is not freed and it is not reachable, then it is a memory leak.

           In order to verify whether an object is reachable, we start from the root list (those
           possible reachable objects).

           However, it is much easier for the checking in the end of a program. We basically think
           that every object should be freed. Thus, we only needs to search the heap list to find
           those unfreed objects. If there is someone, then we reported that and rollback.

           In order to detect those callsites for those memory leakage, we basically maintain
           a hashtable. Whenever there is memory allocation, we check whether this object is a
           possible memory leakage. If yes, then we update corresponding list about how many leakage
           happens on each memory allocation site.

 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <ucontext.h>

#include <list>
#include <new>

#include "selfmap.hh"
#include "threadlocalstatus.h"

#ifdef PRINT_LEAK_OBJECTS

inline size_t aligndown(size_t addr, size_t alignto) { return (addr & ~(alignto - 1)); }

class leakcheck {
public:
  static void searchHeapPointersInsideGlobals();
  static void doSlowLeakCheck(unsigned long begin, unsigned long end);
  static void exploreHeapObject(unsigned long addr);
  static void mark();
  static void sweep();
    static bool isPossibleHeapPointer(unsigned long addr);
    static void checkInsertUnexploredList(unsigned long addr);
    static void searchHeapPointers(unsigned long start, unsigned long end);
    static void searchHeapPointers(ucontext_t* context);
    static void searchHeapPointersInsideStack(void* start);
    static void debugPrintQueue();
    static void handler(int signo);
    static thread_local std::list<unsigned long> _unexploredObjects;
    static size_t _totalLeakageSize;
    static unsigned long _heapBegin;
    static unsigned long _heapEnd;
};


#endif

#endif
