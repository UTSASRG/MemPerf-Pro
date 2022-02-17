#ifndef SRC_MEMORYUSAGE_H
#define SRC_MEMORYUSAGE_H

#include <mutex>
#include <time.h>
//#include "memwaste.h"
#include "definevalues.h"
#include "globalstatus.h"
//#include "structs.h"
//#include "leakcheck.hh"

#ifdef MEMORY

struct TotalMemoryUsage {
    int64_t realMemoryUsage;
    int64_t totalMemoryUsage;
    bool isLowerThan(TotalMemoryUsage newTotalMemoryUsage, unsigned int interval);
    bool isLowerThan(TotalMemoryUsage newTotalMemoryUsage);
    void ifLowerThanReplace(TotalMemoryUsage newTotalMemoryUsage);
#ifdef OPEN_DEBUG
    void debugPrint();
#endif
};

class MemoryUsage {
public:
    static std::mutex mtx;
    static thread_local TotalMemoryUsage threadLocalMemoryUsage, maxThreadLocalMemoryUsage;
    static TotalMemoryUsage globalThreadLocalMemoryUsage;
    static TotalMemoryUsage globalMemoryUsage, maxGlobalMemoryUsage;
    static int64_t maxRealMemoryUsage;
    static void addToMemoryUsage(unsigned int size, unsigned int newTouchePageBytes);
    static void subRealSizeFromMemoryUsage(unsigned int size);
    static void subTotalSizeFromMemoryUsage(unsigned int size);
    static void globalize();
    static void printOutput();

};

#endif

#endif //SRC_MEMORYUSAGE_H