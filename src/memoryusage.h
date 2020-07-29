//
// Created by 86152 on 2020/5/24.
//

#ifndef SRC_MEMORYUSAGE_H
#define SRC_MEMORYUSAGE_H

#include "memwaste.h"
#include "definevalues.h"
#include "globalstatus.h"
#include "structs.h"

class GlobalStatus;
class MemoryWaste;
class ProgramStatus;

class MemoryUsage {
public:
    static thread_local TotalMemoryUsage threadLocalMemoryUsage, maxThreadLocalMemoryUsage;
    static TotalMemoryUsage globalThreadLocalMemoryUsage;
    static TotalMemoryUsage globalMemoryUsage, maxGlobalMemoryUsage;
    static spinlock debugLock;
    static void addToMemoryUsage(size_t size, size_t newTouchePageBytes);
    static void subRealSizeFromMemoryUsage(size_t size);
    static void subTotalSizeFromMemoryUsage(size_t size);
    static void globalize();
    static void printOutput();
    static void clearAbnormalValues();

};

#endif //SRC_MEMORYUSAGE_H