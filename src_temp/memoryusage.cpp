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
    if(maxThreadLocalMemoryUsage.isLowerThan(threadLocalMemoryUsage)) {
        maxThreadLocalMemoryUsage = threadLocalMemoryUsage;
    }

    __atomic_add_fetch(&globalMemoryUsage.realMemoryUsage, size, __ATOMIC_RELAXED);
    __atomic_add_fetch(&globalMemoryUsage.totalMemoryUsage, newTouchePageBytes, __ATOMIC_RELAXED);

    maxRealMemoryUsage = MAX(maxRealMemoryUsage, globalMemoryUsage.realMemoryUsage);

     if(globalMemoryUsage.totalMemoryUsage >= 10*ONE_MB &&
     (globalMemoryUsage.totalMemoryUsage > maxGlobalMemoryUsage.totalMemoryUsage + 200*ONE_MB
     || globalMemoryUsage.totalMemoryUsage > maxGlobalMemoryUsage.totalMemoryUsage * 2) && mtx.try_lock()) {

         maxGlobalMemoryUsage = globalMemoryUsage;
//         updateTimes++;

#ifdef OPEN_BACKTRACE
         Backtrace::recordMem();
#endif

//         MemoryWaste::compareMemoryUsageAndRecordStatus(maxGlobalMemoryUsage);
//#ifdef PRINT_LEAK_OBJECTS
//         if(MemoryWaste::minAddr != (uint64_t)-1 && MemoryWaste::maxAddr) {
//             if(ThreadLocalStatus::numOfFunctions[0] + ThreadLocalStatus::numOfFunctions[1] + ThreadLocalStatus::numOfFunctions[2] +
//             ThreadLocalStatus::numOfFunctions[3] +ThreadLocalStatus::numOfFunctions[4] +
//             ThreadLocalStatus::numOfFunctions[12] + ThreadLocalStatus::numOfFunctions[13] + ThreadLocalStatus::numOfFunctions[14] + ThreadLocalStatus::numOfFunctions[15] +
//             ThreadLocalStatus::numOfFunctions[16] <= 1000) {
//                 ;
//             }  else {
//                 leakcheck::doSlowLeakCheck(MemoryWaste::minAddr, MemoryWaste::maxAddr);
//                 leakcheck::sweep();
//                 fprintf(stderr, "thread %d potential leak = %luKb\n", ThreadLocalStatus::runningThreadIndex, leakcheck::_totalLeakageSize/ONE_KB);
////             }
//             }
//         }
//#endif
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
//    fprintf(stderr, "updateTimes = %u\n", updateTimes);
}

#endif