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
    size_t memAllocated;
    void * frames[8];
    int numberOfFrame;
    static BackTraceMemory newBackTraceMemory();
};

//struct BackTraceContent {
//    size_t memAllocatedAtPeak;
//    size_t memAllocatedAtEnd;
//    void * frames[8];
//    int numberOfFrame;
//    static BackTraceContent newBackTraceContent();
//};

struct BTAddrMemPair {
    void * frames[8];
    int numberOfFrame;
    size_t memory;
};

class Backtrace {
private:
//    static HashMap <void*, BackTraceMemory, PrivateHeap> BTMemMap;
    static spinlock lock;
    static spinlock recordLock;
    static bool compare(BTAddrMemPair a, BTAddrMemPair b);
    static void* ConvertToVMA(void* addr);
    static void ssystem(char* command);
public:
    static void debugPrintTrace();
    static void init();
    static uint64_t doABackTrace(size_t size);
    static void subMem(uint64_t callsiteKey, size_t size);
    static void recordMem();
    static void debugPrintOutput();
    static void printOutput();
};


#endif //SRC_BACKTRACE_H
