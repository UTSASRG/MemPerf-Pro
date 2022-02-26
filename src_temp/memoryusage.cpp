#include "memoryusage.h"

#ifdef MEMORY

std::mutex MemoryUsage::mtx;
thread_local TotalMemoryUsage MemoryUsage::threadLocalMemoryUsage, MemoryUsage::maxThreadLocalMemoryUsage;
TotalMemoryUsage MemoryUsage::globalThreadLocalMemoryUsage;
TotalMemoryUsage MemoryUsage::globalMemoryUsage, MemoryUsage::maxGlobalMemoryUsage;
int64_t MemoryUsage::maxRealMemoryUsage; ///Shows in total memory

//unsigned int updateTimes = 0;

bool TotalMemoryUsage::isLowerThan(TotalMemoryUsage newTotalMemoryUsage, unsigned int interval) {
    return this->totalMemoryUsage + (int64_t)interval < newTotalMemoryUsage.totalMemoryUsage;
}

bool TotalMemoryUsage::isLowerThan(TotalMemoryUsage newTotalMemoryUsage) {
    return (this->totalMemoryUsage < newTotalMemoryUsage.totalMemoryUsage) ||
        ((this->totalMemoryUsage == newTotalMemoryUsage.totalMemoryUsage) && (this->realMemoryUsage < newTotalMemoryUsage.realMemoryUsage));
}

void TotalMemoryUsage::ifLowerThanReplace(TotalMemoryUsage newTotalMemoryUsage) {
    this->realMemoryUsage = MAX(this->realMemoryUsage, newTotalMemoryUsage.realMemoryUsage);
    this->totalMemoryUsage = MAX(this->totalMemoryUsage, newTotalMemoryUsage.totalMemoryUsage);
}


void MemoryUsage::addToMemoryUsage(unsigned int size, unsigned int newTouchePageBytes) {
    threadLocalMemoryUsage.realMemoryUsage += size;
    threadLocalMemoryUsage.totalMemoryUsage += newTouchePageBytes;
/*
        fprintf(stderr, "%u %u %ld %ld %ld %ld\n",
                size, newTouchePageBytes,
                threadLocalMemoryUsage.realMemoryUsage, threadLocalMemoryUsage.totalMemoryUsage,
                maxThreadLocalMemoryUsage.realMemoryUsage, maxThreadLocalMemoryUsage.totalMemoryUsage);
*/
    if(maxThreadLocalMemoryUsage.isLowerThan(threadLocalMemoryUsage)) {
//        fprintf(stderr, "replace\n");
        maxThreadLocalMemoryUsage = threadLocalMemoryUsage;
    }

    __atomic_add_fetch(&globalMemoryUsage.realMemoryUsage, size, __ATOMIC_RELAXED);
    __atomic_add_fetch(&globalMemoryUsage.totalMemoryUsage, newTouchePageBytes, __ATOMIC_RELAXED);

    maxRealMemoryUsage = MAX(maxRealMemoryUsage, globalMemoryUsage.realMemoryUsage);

#ifdef FREQUENT_UPDATE_MEMORY
    if(globalMemoryUsage.totalMemoryUsage > maxGlobalMemoryUsage.totalMemoryUsage) {
#else
     if(globalMemoryUsage.totalMemoryUsage >= 10*ONE_MB &&
     (globalMemoryUsage.totalMemoryUsage > maxGlobalMemoryUsage.totalMemoryUsage + 200*ONE_MB
     || globalMemoryUsage.totalMemoryUsage > maxGlobalMemoryUsage.totalMemoryUsage * 2) && mtx.try_lock()) {
#endif
         maxGlobalMemoryUsage = globalMemoryUsage;
//         updateTimes++;
#ifndef FREQUENT_UPDATE_MEMORY
#ifdef OPEN_BACKTRACE
         Backtrace::recordMem();
#endif
#endif

         MemoryWaste::compareMemoryUsageAndRecordStatus(maxGlobalMemoryUsage);
#ifdef PRINT_LEAK_OBJECTS
//         if((uint64_t)MemoryWaste::minAddr != (uint64_t)-1 && MemoryWaste::maxAddr) {
//             if (ThreadLocalStatus::numOfSampledCountingFunctions[0] +
//                 ThreadLocalStatus::numOfSampledCountingFunctions[1] +
//                 ThreadLocalStatus::numOfSampledCountingFunctions[2] +
//                 ThreadLocalStatus::numOfSampledCountingFunctions[3] +
//                 ThreadLocalStatus::numOfSampledCountingFunctions[4] +
//                 ThreadLocalStatus::numOfSampledCountingFunctions[12] +
//                 ThreadLocalStatus::numOfSampledCountingFunctions[13] +
//                 ThreadLocalStatus::numOfSampledCountingFunctions[14] +
//                 ThreadLocalStatus::numOfSampledCountingFunctions[15] +
//                 ThreadLocalStatus::numOfSampledCountingFunctions[16] > 1000 / RANDOM_PERIOD_FOR_ALLOCS) {
//                 leakcheck::doSlowLeakCheck(MemoryWaste::minAddr, MemoryWaste::maxAddr);
//                 leakcheck::sweep();
//                 fprintf(stderr, "thread %d potential leak = %luKb\n", ThreadLocalStatus::runningThreadIndex,
//                         leakcheck::_totalLeakageSize / ONE_KB);
//             }
//         }
#endif

#ifndef FREQUENT_UPDATE_MEMORY
        mtx.unlock();
#endif
     }

}

void MemoryUsage::subRealSizeFromMemoryUsage(unsigned int size) {
    threadLocalMemoryUsage.realMemoryUsage -= size;
    __atomic_sub_fetch(&globalMemoryUsage.realMemoryUsage, size, __ATOMIC_RELAXED);
}

void MemoryUsage::subTotalSizeFromMemoryUsage(unsigned int size) {
//        fprintf(stderr, "sub %u\n", size);
    threadLocalMemoryUsage.totalMemoryUsage -= size;
    __atomic_sub_fetch(&globalMemoryUsage.totalMemoryUsage, size, __ATOMIC_RELAXED);
}

void MemoryUsage::globalize() {
//        fprintf(stderr, "globalize %d %ld %ld\n", ThreadLocalStatus::runningThreadIndex, maxThreadLocalMemoryUsage.totalMemoryUsage, maxThreadLocalMemoryUsage.totalMemoryUsage);
    globalThreadLocalMemoryUsage.ifLowerThanReplace(maxThreadLocalMemoryUsage);
}

void MemoryUsage::printOutput() {
    GlobalStatus::printTitle((char*)"MEMORY USAGE AT PEAK");
    fprintf(ProgramStatus::outputFile, "thread local max real memory usage                %20ldK\n", globalThreadLocalMemoryUsage.realMemoryUsage/ONE_KB);
    fprintf(ProgramStatus::outputFile, "thread local max total memory usage               %20ldK\n", globalThreadLocalMemoryUsage.totalMemoryUsage/ONE_KB);
    fprintf(ProgramStatus::outputFile, "global max real memory usage                      %20ldK\n", maxRealMemoryUsage/ONE_KB);
    fprintf(ProgramStatus::outputFile, "global max total memory usage                     %20ldK\n", maxGlobalMemoryUsage.totalMemoryUsage/ONE_KB);
//    fprintf(stderr, "updateTimes = %u\n", updateTimes);
}

#endif