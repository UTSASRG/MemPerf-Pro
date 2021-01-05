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
    static uint64_t criticalCycleDepend;
    static uint64_t replacedCriticalCycleDepend;
#ifdef OPEN_COUNTING_EVENT
    static PerfReadInfo criticalCountingEvent;
#endif
    static uint64_t threadCycle[MAX_THREAD_NUMBER];
    static uint64_t threadReplacedCycle[MAX_THREAD_NUMBER];
#ifdef OPEN_COUNTING_EVENT
    static PerfReadInfo threadCountingEvents[MAX_THREAD_NUMBER];
    static thread_local PerfReadInfo startCountingEvent;
    static thread_local PerfReadInfo stopCountingEvent;
    static thread_local PerfReadInfo countingEvent;
#endif
    static thread_local unsigned int numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static thread_local uint64_t functionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
    static size_t replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];///avg

    static unsigned short replacedMiddleObjectThreshold;
    static unsigned int replacedLargeObjectThreshold;
    static thread_local uint64_t outsideStartCycle;
    static thread_local uint64_t outsideStopCycle;
    static thread_local uint64_t outsideCycle;
    static thread_local uint64_t outsideCycleMinus;

//    static thread_local uint64_t faultedPages;
//    static uint64_t cyclePerPageFault;

    static FILE * predictorInfoFile;

    static bool lastThreadDepend;
    static unsigned short lastThreadIndex;

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
    static void readFunctionCyclesFromInfo(char*token);
    static void readMiddleObjectThresholdFromInfo(char*token);
    static void readLargeObjectThresholdFromInfo(char*token);
    static void readPageFaultCycleFromInfo(char*token);
    static void fopenPredictorInfoFile();
    static void readPredictorInfoFile();
    static void openPredictorInfoFile();
};

#endif //SRC_PREDICTOR_H
