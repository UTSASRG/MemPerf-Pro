#include "predictor.h"

uint64_t Predictor::criticalCycle;
uint64_t Predictor::replacedCriticalCycle;
uint64_t Predictor::threadCycle[MAX_THREAD_NUMBER];
uint64_t Predictor::threadReplacedCycle[MAX_THREAD_NUMBER];
thread_local uint64_t Predictor::numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local uint64_t Predictor::functionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
const int64_t Predictor::replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local uint64_t Predictor::outsideStartCycle;
thread_local uint64_t Predictor::outsideStopCycle;
thread_local uint64_t Predictor::outsideCycle;

void Predictor::cleanStageData() {
    memset(threadCycle, 0, sizeof(uint64_t) * ThreadLocalStatus::totalNumOfThread);
    memset(threadReplacedCycle, 0, sizeof(uint64_t) * ThreadLocalStatus::totalNumOfThread);
    memset(numOfFunctions, 0, sizeof(uint64_t) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);
    memset(functionCycles, 0, sizeof(uint64_t) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);
    outsideCycle = 0;
}

void Predictor::outsideCycleStart() {
    outsideStartCycle = rdtscp();
}

void Predictor::outsideCyclesStop() {
    outsideStopCycle = rdtscp();
    outsideCycle += outsideStopCycle - outsideStartCycle;
}

void Predictor::threadEnd() {
    threadCycle[ThreadLocalStatus::runningThreadIndex] += outsideCycle;
    threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] += outsideCycle;
    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        threadCycle[ThreadLocalStatus::runningThreadIndex] += functionCycles[allocationType];
        if(replacedFunctionCycles[allocationType] == -1) {
            threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] += functionCycles[allocationType];
        } else {
            threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] += numOfFunctions[allocationType] * replacedFunctionCycles[allocationType];
        }
    }
//    fprintf(stderr, "thread end %lu %lu %lu: %lu %lu %lu %lu %lu\n",
//            ThreadLocalStatus::runningThreadIndex, threadCycle[ThreadLocalStatus::runningThreadIndex], threadReplacedCycle[ThreadLocalStatus::runningThreadIndex],
//            functionCycles[12], functionCycles[14], functionCycles[16], functionCycles[17], functionCycles[19]);

}

void Predictor::stopSerial() {
    criticalCycle += outsideCycle;
    replacedCriticalCycle += outsideCycle;
    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        criticalCycle += functionCycles[allocationType];
        if(replacedFunctionCycles[allocationType] == -1) {
            replacedCriticalCycle += functionCycles[allocationType];
        } else {
            replacedCriticalCycle += numOfFunctions[allocationType] * replacedFunctionCycles[allocationType];
        }
    }
//    fprintf(stderr, "start parallel %lu %lu\n", criticalCycle, replacedCriticalCycle);
    cleanStageData();
}

void Predictor::stopParallel() {
    uint64_t criticalStageCycle = 0;
    uint64_t criticalReplacedStageCycle = 0;
    for(unsigned int index = 1; index < ThreadLocalStatus::totalNumOfThread; ++index) {
        if(threadCycle[index] && threadReplacedCycle[index]) {
            criticalStageCycle = MAX(criticalStageCycle, threadCycle[index]);
            criticalReplacedStageCycle = MAX(criticalReplacedStageCycle, threadReplacedCycle[index]);
        }
    }
    criticalCycle += criticalStageCycle;
    replacedCriticalCycle += criticalReplacedStageCycle;
//    fprintf(stderr, "stop parallel %lu %lu\n", criticalCycle, replacedCriticalCycle);
    cleanStageData();
}

void Predictor::printOutput() {
    GlobalStatus::printTitle((char*)"PREDICTION");
    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(replacedFunctionCycles[allocationType] != -1) {
            fprintf(ProgramStatus::outputFile, "replacing cycles in %s %20lu\n", allocationTypeOutputString[allocationType], replacedFunctionCycles[allocationType]);
        }
    }
    fprintf(ProgramStatus::outputFile, "original critical cycle %20lu\n", criticalCycle);
    fprintf(ProgramStatus::outputFile, "predicted critical cycle%20lu\n", replacedCriticalCycle);
    if(criticalCycle) {
        fprintf(ProgramStatus::outputFile, "ratio %3lu%%\n", replacedCriticalCycle*100/criticalCycle);
    } else {
        fprintf(ProgramStatus::outputFile, "ratio 100%%\n");
    }
    if(criticalCycle && replacedCriticalCycle < criticalCycle) {
        fprintf(ProgramStatus::outputFile, "speed up %3lu%%\n", (criticalCycle-replacedCriticalCycle)*100/replacedCriticalCycle);
    } else {
        fprintf(ProgramStatus::outputFile, "speed up 0%%\n");
    }

}