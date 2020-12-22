//
// Created by 86152 on 2020/5/24.
//

#ifndef SRC_MEMORYUSAGE_H
#define SRC_MEMORYUSAGE_H

#include <mutex>
#include <time.h>
#include "memwaste.h"
#include "definevalues.h"
#include "globalstatus.h"
#include "structs.h"
#include "leakcheck.hh"
class MemoryUsage {
public:
//    static spinlock lock;
//    static bool helpMarked[MAX_THREAD_NUMBER];
    static std::mutex mtx;
    static thread_local TotalMemoryUsage threadLocalMemoryUsage, maxThreadLocalMemoryUsage;
    static TotalMemoryUsage globalThreadLocalMemoryUsage;
    static TotalMemoryUsage globalMemoryUsage, maxGlobalMemoryUsage;
    static int64_t maxRealMemoryUsage;
#ifdef PRINT_MEM_DETAIL_THRESHOLD
    static spinlock debugLock;
#endif
    static void addToMemoryUsage(unsigned int size, unsigned int newTouchePageBytes);
    static void subRealSizeFromMemoryUsage(unsigned int size);
    static void subTotalSizeFromMemoryUsage(unsigned int size);
    static void globalize();
    static void printOutput();
#ifdef PRINT_MEM_DETAIL_THRESHOLD
    static void stopIfMaxMemReached(int64_t maxInKb);
#endif
//    static void endCheck();

};

#endif //SRC_MEMORYUSAGE_H