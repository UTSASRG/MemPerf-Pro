#ifndef SRC_BACKTRACE_H
#define SRC_BACKTRACE_H

#include <execinfo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <link.h>
#include <algorithm>
#include "definevalues.h"
#include "hashfuncs.hh"
#include "spinlock.hh"
#include "hashmap.hh"
#include "threadlocalstatus.h"
#include "programstatus.h"
#include "globalstatus.h"
#include "libmallocprof.h"

struct BackTraceMemory {
    size_t memAllocated[MAX_THREAD_NUMBER];
    static BackTraceMemory newBackTraceMemory();
};

struct BTAddrMemPair {
    void * address;
    size_t memory;
};

class Backtrace {
private:
//    static HashMap <void*, BackTraceMemory, PrivateHeap> BTMemMap;
    static spinlock lock;
    static spinlock recordLock;
    static bool compare(BTAddrMemPair a, BTAddrMemPair b);
    static size_t ConvertToVMA(size_t addr);
public:
    static void debugPrintTrace();
    static void init();
    static void * doABackTrace(size_t size);
    static void subMem(void * addr, size_t size);
    static void recordMem();
    static void debugPrintOutput();
    static void printOutput();
};


#endif //SRC_BACKTRACE_H
