//
// Created by 86152 on 2020/5/24.
//

#ifndef SRC_MEMORYUSAGE_H
#define SRC_MEMORYUSAGE_H

#include "memwaste.h"
#include "definevalues.h"

struct TotalMemoryUsage {
    uint64_t realMemoryUsage;
    uint64_t totalMemoryUsage;
    bool isLowerThan(TotalMemoryUsage newTotalMemoryUsage, size_t interval) {
        return this->totalMemoryUsage + interval < newTotalMemoryUsage.totalMemoryUsage;
    }
    bool isLowerThan(TotalMemoryUsage newTotalMemoryUsage) {
        return this->totalMemoryUsage < newTotalMemoryUsage.totalMemoryUsage;
    }
};

class MemoryUsage {
private:
    static thread_local TotalMemoryUsage threadLocalMemoryUsage, maxThreadLocalMemoryUsage;
    static TotalMemoryUsage globalMemoryUsage, maxGlobalMemoryUsage;

public:
    static void addToMemoryUsage(size_t size, size_t newTouchePageBytes) {
        threadLocalMemoryUsage.realMemoryUsage += size;
        threadLocalMemoryUsage.totalMemoryUsage += newTouchePageBytes;
        if(maxThreadLocalMemoryUsage.isLowerThan(threadLocalMemoryUsage, ONE_MB)) {
            maxThreadLocalMemoryUsage = threadLocalMemoryUsage;
        }

        __atomic_add_fetch(&globalMemoryUsage.realMemoryUsage, size, __ATOMIC_RELAXED);
        __atomic_add_fetch(&globalMemoryUsage.totalMemoryUsage, newTouchePageBytes, __ATOMIC_RELAXED);
        if(maxGlobalMemoryUsage.isLowerThan(globalMemoryUsage, ONE_MB)) {
            maxGlobalMemoryUsage = globalMemoryUsage;
            MemoryWaste::compareMemoryUsageAndRecordStatus(maxGlobalMemoryUsage);
        }
    }

    static void subRealSizeFromMemoryUsage(size_t size) {
        threadLocalMemoryUsage.realMemoryUsage -= size;
        __atomic_sub_fetch(&globalMemoryUsage.realMemoryUsage, size, __ATOMIC_RELAXED);
    }

    static void subTotalSizeFromMemoryUsage(size_t size) {
        threadLocalMemoryUsage.totalMemoryUsage -= size;
        __atomic_sub_fetch(&globalMemoryUsage.totalMemoryUsage, size, __ATOMIC_RELAXED);
    }

};

#endif //SRC_MEMORYUSAGE_H