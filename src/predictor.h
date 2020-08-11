#ifndef SRC_PREDICTOR_H
#define SRC_PREDICTOR_H

#include "definevalues.h"
#include "threadlocalstatus.h"
#include "globalstatus.h"

class Predictor {

public:
    static uint64_t totalCycle;
    static uint64_t criticalCycle;
    static uint64_t replacedCriticalCycle;
    static PerfReadInfo criticalCountingEvent;
    static uint64_t threadCycle[MAX_THREAD_NUMBER];
    static uint64_t threadReplacedCycle[MAX_THREAD_NUMBER];
    static PerfReadInfo threadCountingEvents[MAX_THREAD_NUMBER];
    static thread_local PerfReadInfo startCountingEvent;
    static thread_local PerfReadInfo stopCountingEvent;
    static thread_local PerfReadInfo countingEvent;
    static thread_local uint64_t numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local uint64_t functionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
//    static constexpr int64_t replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//                                                                                       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
//    static constexpr int64_t replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1,
//                                                                                           0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1};
//    static constexpr int64_t replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {172, -1, 64, -1, 8006, 113, -1, 9559, -1, -1, -1, -1,
//                                                                                           475, -1, 279, -1, 2144, 219, -1, 2761, -1, -1, -1, -1}; ///fault3000
//    static constexpr int64_t replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {113, -1, 47, -1, 1529, 62, -1, 1490, -1, -1, -1, -1,
//                                                                                           577, -1, 369, -1, 2758, 234, -1, 2924, -1, -1, -1, -1}; ///raytrace
    static constexpr int64_t replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {191, -1, 64, -1, 9821, 113, -1, 11681, -1, -1, -1, -1,
                                                                                           577, -1, 369, -1, 2758, 234, -1, 2924, -1, -1, -1, -1}; ///avg

    static const uint64_t replacedMiddleObjectThreshold = 188424;
    static const uint64_t replacedLargeObjectThreshold = 188424;
    static thread_local uint64_t outsideStartCycle;
    static thread_local uint64_t outsideStopCycle;
    static thread_local uint64_t outsideCycle;
    static thread_local uint64_t outsideCycleMinus;

    static thread_local uint64_t faultedPages;
    static const uint64_t cyclePerPageFault = 2900;

    static void globalInit();
    static void threadInit();
    static void cleanStageData();
    static void subOutsideCycle(uint64_t cycles);
    static void outsideCycleStart();
    static void outsideCyclesStop();
    static void outsideCountingEventsStart();
    static void outsideCountingEventsStop();
    static void threadEnd();
    static void stopSerial();
    static void stopParallel();
    static void printOutput();
};

#endif //SRC_PREDICTOR_H
