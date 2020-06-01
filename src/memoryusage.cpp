#include "memoryusage.h"

void MemoryUsage::addToMemoryUsage(size_t size, size_t newTouchePageBytes) {
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

void MemoryUsage::subRealSizeFromMemoryUsage(size_t size) {
    threadLocalMemoryUsage.realMemoryUsage -= size;
    __atomic_sub_fetch(&globalMemoryUsage.realMemoryUsage, size, __ATOMIC_RELAXED);
}

void MemoryUsage::subTotalSizeFromMemoryUsage(size_t size) {
    threadLocalMemoryUsage.totalMemoryUsage -= size;
    __atomic_sub_fetch(&globalMemoryUsage.totalMemoryUsage, size, __ATOMIC_RELAXED);
}

void MemoryUsage::printOutput() {
    GlobalStatus::printTitle((char*)"MEMORY USAGE");
    fprintf(ProgramStatus::outputFile, "thread local max real memory usage %20lu\n", maxThreadLocalMemoryUsage.realMemoryUsage);
    fprintf(ProgramStatus::outputFile, "thread local max total memory usage %20lu\n", maxThreadLocalMemoryUsage.totalMemoryUsage);
    fprintf(ProgramStatus::outputFile, "global max real memory usage %20lu\n", maxGlobalMemoryUsage.realMemoryUsage);
    fprintf(ProgramStatus::outputFile, "global local max real memory usage %20lu\n", maxGlobalMemoryUsage.realMemoryUsage);
}