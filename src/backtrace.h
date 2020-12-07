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

struct BackTraceMemory {
    uint8_t numberOfFrame;
//    unsigned int memAllocated;
    uint64_t memAllocated;
    void * frames[7];
    static BackTraceMemory newBackTraceMemory();
};

struct BTAddrMemPair {
    uint8_t numberOfFrame;
    unsigned int memory;
//    unsigned int memory;
    void * frames[7];
};

class Backtrace {
private:
//    static spinlock lock;
//    static spinlock recordLock;
    static bool compare(BTAddrMemPair a, BTAddrMemPair b);
    static void* ConvertToVMA(void* addr);
    static void ssystem(char* command);
public:
#ifdef OPEN_DEBUG
    static void debugPrintTrace();
#endif
    static void init();
    static uint8_t doABackTrace(unsigned int size);
    static void subMem(uint8_t callsiteKey, unsigned int size);
    static void recordMem();
    static void debugPrintOutput();
    static void printOutput();
};

#endif


#endif //SRC_BACKTRACE_H
