#ifndef SRC_BACKTRACE_H
#define SRC_BACKTRACE_H

#include <execinfo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <link.h>
#include <algorithm>
#include <string>
#include "definevalues.h"
#include "hashfuncs.hh"
#include "spinlock.hh"
#include "hashmap.hh"
#include "threadlocalstatus.h"
#include "programstatus.h"
#include "globalstatus.h"
#include "libmallocprof.h"

#ifdef OPEN_BACKTRACE

#define MAX_SOURCE_LENGTH 2048
#define NUM_CALLKEY 256

struct BackTraceMemory {
    uint8_t numberOfFrame;
//    unsigned int memAllocated;
    uint64_t memAllocated;
    uint64_t memLeaked;
    void * frames[7];
    static BackTraceMemory newBackTraceMemory();
};

struct BTAddrMemPair {
    uint8_t numberOfFrame;
    unsigned int memory;
//    unsigned int memory;
    void * frames[7];
};

namespace Backtrace {
//     spinlock lock;
//     spinlock recordLock;
    bool compare(BTAddrMemPair a, BTAddrMemPair b);
    void* ConvertToVMA(void* addr);
     void ssystem(char* command);
     void ssystem2(char* command);
     void init();
     uint8_t doABackTrace(unsigned int null);
    void addLeak(uint8_t callsiteKey, unsigned int size);
     void subMem(uint8_t callsiteKey, unsigned int size);
     void recordMem();
     void printOutput();
    void printLeak();
     void printCallSite(uint8_t callKey);
};

#endif


#endif //SRC_BACKTRACE_H
