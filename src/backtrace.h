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

struct BackTraceMemory {
    unsigned short numberOfFrame;
    unsigned int memAllocated;
    void * frames[8];
    static BackTraceMemory newBackTraceMemory();
};

struct BTAddrMemPair {
    unsigned short numberOfFrame;
    unsigned int memory;
    void * frames[8];
};

class Backtrace {
private:
    static spinlock lock;
    static spinlock recordLock;
    static bool compare(BTAddrMemPair a, BTAddrMemPair b);
    static void* ConvertToVMA(void* addr);
    static void ssystem(char* command);
public:
#ifdef OPEN_DEBUG
    static void debugPrintTrace();
#endif
    static void init();
    static uint64_t doABackTrace(unsigned int size);
    static void subMem(uint64_t callsiteKey, unsigned int size);
    static void recordMem();
    static void debugPrintOutput();
    static void printOutput();
};


#endif //SRC_BACKTRACE_H
