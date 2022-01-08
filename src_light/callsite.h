#ifndef SAMPLING_CPP_CALLSITE_H
#define SAMPLING_CPP_CALLSITE_H

#include <stdint.h>
#include <execinfo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <link.h>
#include <algorithm>
#include <string>
#include <errno.h>
#include <atomic>
#include "hashfuncs.hh"
#include "hashmap.hh"
#include "threadlocalstatus.h"
#include "libmallocprof.h"

#define MAX_SOURCE_LENGTH 2048

#define NUM_CALLKEY 256
#define NUM_VICTIM_CALLKEY 16

//#define RETURN_OFFSET 284 /// O0
//#define RETURN_OFFSET 236 /// O2
#define RETURN_OFFSET 268 /// O3, Ofast
#define BREAKPAD_NO_TERMINATE_THREAD 1

struct CallKeyHashLocksSet {
    spinlock locks[NUM_CALLKEY];
    void lock(uint8_t key);
    void unlock(uint8_t key);
};

namespace Callsite {
    extern uint16_t numCallKey;

    uint8_t getCallKey(uint8_t oldCallKey);
    void * ConvertToVMA(void* addr);
    void ssystem(char * command);
    void printCallSite(uint8_t callKey);
}

#endif //SAMPLING_CPP_CALLSITE_H
