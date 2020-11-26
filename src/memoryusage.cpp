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
time_t MemoryUsage::lastTimeUpdated;
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

#ifdef PRINT_MEM_DETAIL_THRESHOLD
    stopIfMaxMemReached(PRINT_MEM_DETAIL_THRESHOLD);
#endif
    maxRealMemoryUsage = MAX(maxRealMemoryUsage, globalMemoryUsage.realMemoryUsage);
//    if(maxGlobalMemoryUsage.isLowerThan(globalMemoryUsage, ONE_MB) &&
//    helpMarked[ThreadLocalStatus::runningThreadIndex] == false) {
//        if(MemoryWaste::minAddr != -1 && MemoryWaste::maxAddr) {
//            leakcheck::doSlowLeakCheck(MemoryWaste::minAddr, MemoryWaste::maxAddr);
//        }
//        helpMarked[ThreadLocalStatus::runningThreadIndex] = true;
//    }
     if(maxGlobalMemoryUsage.isLowerThan(globalMemoryUsage, ONE_MB) && time(NULL) > lastTimeUpdated+1
     && mtx.try_lock()) {
         lastTimeUpdated = time(NULL);
         maxGlobalMemoryUsage = globalMemoryUsage;
         updateTimes++;
         Backtrace::recordMem();
         MemoryWaste::compareMemoryUsageAndRecordStatus(maxGlobalMemoryUsage);
         if(MemoryWaste::minAddr != -1 && MemoryWaste::maxAddr) {
             if(updateTimes%5 == 0) {
//                 memset(helpMarked, 0, ThreadLocalStatus::totalNumOfThread*sizeof(bool));
                 leakcheck::doSlowLeakCheck(MemoryWaste::minAddr, MemoryWaste::maxAddr);
                 leakcheck::sweep();
                 fprintf(stderr, "leak = %luKb\n", leakcheck::_totalLeakageSize/ONE_KB);
             }
         }
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