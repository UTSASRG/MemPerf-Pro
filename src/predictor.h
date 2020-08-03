#ifndef SRC_PREDICTOR_H
#define SRC_PREDICTOR_H

#include "definevalues.h"
#include "threadlocalstatus.h"
#include "globalstatus.h"

class Predictor {

public:
    static uint64_t criticalCycle;
    static uint64_t replacedCriticalCycle;
    static uint64_t threadCycle[MAX_THREAD_NUMBER];
    static uint64_t threadReplacedCycle[MAX_THREAD_NUMBER];
    static thread_local uint64_t numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local uint64_t functionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
//    static constexpr int64_t replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
//                                                                                       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
//    static constexpr int64_t replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1,
//                                                                                           0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1};
    static constexpr int64_t replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA] = {199, -1, -1, -1, -1, 467, -1, -1, -1, -1, -1, -1,
                                                                                           8403, -1, 196, -1, -1, 137, -1, -1, -1, -1, -1, -1}; ///tcmalloc-swaptions
    static thread_local uint64_t outsideStartCycle;
    static thread_local uint64_t outsideStopCycle;
    static thread_local uint64_t outsideCycle;

    static void cleanStageData();
    static void outsideCycleStart();
    static void outsideCyclesStop();
    static void threadEnd();
    static void stopSerial();
    static void stopParallel();
    static void printOutput();
};

#endif //SRC_PREDICTOR_H
