#include "memoryusage.h"

thread_local TotalMemoryUsage MemoryUsage::threadLocalMemoryUsage, MemoryUsage::maxThreadLocalMemoryUsage;
TotalMemoryUsage MemoryUsage::globalThreadLocalMemoryUsage;
TotalMemoryUsage MemoryUsage::globalMemoryUsage, MemoryUsage::maxGlobalMemoryUsage;
spinlock MemoryUsage::debugLock;

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

void MemoryUsage::globalize() {
    globalThreadLocalMemoryUsage.ifLowerThanReplace(maxThreadLocalMemoryUsage);
}

void MemoryUsage::printOutput() {
    GlobalStatus::printTitle((char*)"MEMORY USAGE");
    fprintf(ProgramStatus::outputFile, "thread local max real memory usage                %20ldK\n", globalThreadLocalMemoryUsage.realMemoryUsage/ONE_KB);
    fprintf(ProgramStatus::outputFile, "thread local max total memory usage               %20ldK\n", globalThreadLocalMemoryUsage.totalMemoryUsage/ONE_KB);
    fprintf(ProgramStatus::outputFile, "global max real memory usage                      %20ldK\n", maxGlobalMemoryUsage.realMemoryUsage/ONE_KB);
    fprintf(ProgramStatus::outputFile, "global max total memory usage                     %20ldK\n", maxGlobalMemoryUsage.totalMemoryUsage/ONE_KB);
}

void MemoryUsage::clearAbnormalValues() {

    while(globalMemoryUsage.totalMemoryUsage < globalMemoryUsage.realMemoryUsage) {
        __atomic_add_fetch(&globalMemoryUsage.totalMemoryUsage, PAGESIZE, __ATOMIC_RELAXED);
    }

}