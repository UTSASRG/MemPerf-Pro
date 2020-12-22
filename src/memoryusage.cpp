#include "memoryusage.h"

//bool MemoryUsage::helpMarked[MAX_THREAD_NUMBER];
//spinlock MemoryUsage::lock;
std::mutex MemoryUsage::mtx;
thread_local TotalMemoryUsage MemoryUsage::threadLocalMemoryUsage, MemoryUsage::maxThreadLocalMemoryUsage;
TotalMemoryUsage MemoryUsage::globalThreadLocalMemoryUsage;
TotalMemoryUsage MemoryUsage::globalMemoryUsage, MemoryUsage::maxGlobalMemoryUsage;
int64_t MemoryUsage::maxRealMemoryUsage; ///Shows in total memory
#ifdef PRINT_MEM_DETAIL_THRESHOLD
spinlock MemoryUsage::debugLock;
#endif
//
unsigned int updateTimes = 0;

void MemoryUsage::addToMemoryUsage(unsigned int size, unsigned int newTouchePageBytes) {
    threadLocalMemoryUsage.realMemoryUsage += size;
    threadLocalMemoryUsage.totalMemoryUsage += newTouchePageBytes;
    if(maxThreadLocalMemoryUsage.isLowerThan(threadLocalMemoryUsage)) {
        maxThreadLocalMemoryUsage = threadLocalMemoryUsage;
    }

    __atomic_add_fetch(&globalMemoryUsage.realMemoryUsage, size, __ATOMIC_RELAXED);
    __atomic_add_fetch(&globalMemoryUsage.totalMemoryUsage, newTouchePageBytes, __ATOMIC_RELAXED);
//    fprintf(stderr, "%u, %u, %lu, %lu\n",
//            size, newTouchePageBytes, globalMemoryUsage.realMemoryUsage, globalMemoryUsage.totalMemoryUsage);
//    if(globalMemoryUsage.realMemoryUsage > globalMemoryUsage.totalMemoryUsage) {
//        abort();
//    }

#ifdef PRINT_MEM_DETAIL_THRESHOLD
    stopIfMaxMemReached(PRINT_MEM_DETAIL_THRESHOLD);
#endif
    maxRealMemoryUsage = MAX(maxRealMemoryUsage, globalMemoryUsage.realMemoryUsage);

     if(globalMemoryUsage.totalMemoryUsage >= 100*ONE_MB &&
     (globalMemoryUsage.totalMemoryUsage > maxGlobalMemoryUsage.totalMemoryUsage + 400*ONE_MB
     || globalMemoryUsage.totalMemoryUsage > maxGlobalMemoryUsage.totalMemoryUsage * 2)
     && mtx.try_lock()) {
         maxGlobalMemoryUsage = globalMemoryUsage;
         updateTimes++;
#ifdef OPEN_BACKTRACE
         Backtrace::recordMem();
#endif
         MemoryWaste::compareMemoryUsageAndRecordStatus(maxGlobalMemoryUsage);
#ifdef PRINT_LEAK_OBJECTS
         if(MemoryWaste::minAddr != (uint64_t)-1 && MemoryWaste::maxAddr) {
             if(ThreadLocalStatus::numOfFunctions[0] + ThreadLocalStatus::numOfFunctions[1] + ThreadLocalStatus::numOfFunctions[2] +
             ThreadLocalStatus::numOfFunctions[3] +ThreadLocalStatus::numOfFunctions[4] +
             ThreadLocalStatus::numOfFunctions[12] + ThreadLocalStatus::numOfFunctions[13] + ThreadLocalStatus::numOfFunctions[14] + ThreadLocalStatus::numOfFunctions[15] +
             ThreadLocalStatus::numOfFunctions[16] <= 1000) {
                 ;
             }  else {
                 leakcheck::doSlowLeakCheck(MemoryWaste::minAddr, MemoryWaste::maxAddr);
                 leakcheck::sweep();
                 fprintf(stderr, "thread %d potential leak = %luKb\n", ThreadLocalStatus::runningThreadIndex, leakcheck::_totalLeakageSize/ONE_KB);
//             }
             }
         }
#endif
        mtx.unlock();
     }

}

void MemoryUsage::subRealSizeFromMemoryUsage(unsigned int size) {
    threadLocalMemoryUsage.realMemoryUsage -= size;
    __atomic_sub_fetch(&globalMemoryUsage.realMemoryUsage, size, __ATOMIC_RELAXED);
}

void MemoryUsage::subTotalSizeFromMemoryUsage(unsigned int size) {
    threadLocalMemoryUsage.totalMemoryUsage -= size;
    __atomic_sub_fetch(&globalMemoryUsage.totalMemoryUsage, size, __ATOMIC_RELAXED);
}

void MemoryUsage::globalize() {
    globalThreadLocalMemoryUsage.ifLowerThanReplace(maxThreadLocalMemoryUsage);
}

void MemoryUsage::printOutput() {
    GlobalStatus::printTitle((char*)"MEMORY USAGE AT PEAK");
    fprintf(ProgramStatus::outputFile, "thread local max real memory usage                %20ldK\n", globalThreadLocalMemoryUsage.realMemoryUsage/ONE_KB);
    fprintf(ProgramStatus::outputFile, "thread local max total memory usage               %20ldK\n", globalThreadLocalMemoryUsage.totalMemoryUsage/ONE_KB);
    fprintf(ProgramStatus::outputFile, "global max real memory usage                      %20ldK\n", maxRealMemoryUsage/ONE_KB);
    fprintf(ProgramStatus::outputFile, "global max total memory usage                     %20ldK\n", maxGlobalMemoryUsage.totalMemoryUsage/ONE_KB);
    fprintf(stderr, "updateTimes = %u\n", updateTimes);
}

#ifdef PRINT_MEM_DETAIL_THRESHOLD
void MemoryUsage::stopIfMaxMemReached(int64_t maxInKb) {
    if(maxGlobalMemoryUsage.totalMemoryUsage/ONE_KB > maxInKb) {
        debugLock.lock();
        fprintf(stderr, "total memory = %ld\n", maxGlobalMemoryUsage.totalMemoryUsage/ONE_KB);
        ShadowMemory::printAddressRange();
        ShadowMemory::printAllPages();
        fprintf(stderr, "finished\n");
        abort();
        debugLock.unlock();
    }
}
#endif

//void MemoryUsage::endCheck() {
//    if(maxGlobalMemoryUsage.isLowerThan(globalMemoryUsage)) {
//        maxGlobalMemoryUsage = globalMemoryUsage;
//        Backtrace::recordMem();
//        MemoryWaste::compareMemoryUsageAndRecordStatus(maxGlobalMemoryUsage);
//    }
//}