#include "predictor.h"

uint64_t Predictor::totalCycle;
uint64_t Predictor::criticalCycle;
uint64_t Predictor::replacedCriticalCycle;
uint64_t Predictor::criticalCycleDepend;
uint64_t Predictor::replacedCriticalCycleDepend;
#ifdef OPEN_COUNTING_EVENT
PerfReadInfo Predictor::criticalCountingEvent;
#endif

uint64_t Predictor::threadCycle[MAX_THREAD_NUMBER];
uint64_t Predictor::threadReplacedCycle[MAX_THREAD_NUMBER];
#ifdef OPEN_COUNTING_EVENT
PerfReadInfo Predictor::threadCountingEvents[MAX_THREAD_NUMBER];

thread_local PerfReadInfo Predictor::startCountingEvent;
thread_local PerfReadInfo Predictor::stopCountingEvent;
thread_local PerfReadInfo Predictor::countingEvent;
#endif

thread_local unsigned int Predictor::numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local uint64_t Predictor::functionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
size_t Predictor::replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];

unsigned short Predictor::replacedMiddleObjectThreshold;
unsigned int Predictor::replacedLargeObjectThreshold;

thread_local uint64_t Predictor::outsideStartCycle;
thread_local uint64_t Predictor::outsideStopCycle;
thread_local uint64_t Predictor::outsideCycle;
thread_local uint64_t Predictor::outsideCycleMinus;

//thread_local uint64_t Predictor::faultedPages;
//uint64_t Predictor::cyclePerPageFault;

FILE * Predictor::predictorInfoFile;

bool Predictor::lastThreadDepend;
unsigned short Predictor::lastThreadIndex;

void Predictor::globalInit() {
    openPredictorInfoFile();
    totalCycle = 0;
    criticalCycle = 0;
    replacedCriticalCycle = 0;
    criticalCycleDepend = 0;
    replacedCriticalCycleDepend = 0;
#ifdef OPEN_COUNTING_EVENT
    memset(&criticalCountingEvent, 0, sizeof(PerfReadInfo));

    memset(&countingEvent, 0, sizeof(PerfReadInfo));
#endif

    memset(numOfFunctions, 0, sizeof(unsigned int) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);
    memset(functionCycles, 0, sizeof(uint64_t) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);

    outsideCycle = 0;
//    faultedPages = 0;
}

void Predictor::threadInit() {
#ifdef OPEN_COUNTING_EVENT
    memset(&countingEvent, 0, sizeof(PerfReadInfo));
#endif

    memset(numOfFunctions, 0, sizeof(unsigned int) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);
    memset(functionCycles, 0, sizeof(uint64_t) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);

    outsideCycle = 0;
//    faultedPages = 0;
}

void Predictor::cleanStageData() {
    memset(threadCycle, 0, sizeof(uint64_t) * ThreadLocalStatus::totalNumOfThread);
    memset(threadReplacedCycle, 0, sizeof(uint64_t) * ThreadLocalStatus::totalNumOfThread);
#ifdef OPEN_COUNTING_EVENT
    memset(threadCountingEvents, 0, sizeof(PerfReadInfo) * ThreadLocalStatus::totalNumOfThread);

    memset(&countingEvent, 0, sizeof(PerfReadInfo));
#endif

    memset(numOfFunctions, 0, sizeof(unsigned int) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);
    memset(functionCycles, 0, sizeof(uint64_t) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);

    outsideCycle = 0;
//    faultedPages = 0;

}

void Predictor::subOutsideCycle(uint64_t cycles) {
    outsideCycleMinus += cycles;
}

void Predictor::outsideCycleStart() {
    outsideStartCycle = rdtscp();
}

void Predictor::outsideCyclesStop() {
    if(outsideStartCycle) {
        outsideStopCycle = rdtscp();
        if(outsideStopCycle > outsideStartCycle) {
            outsideCycle += outsideStopCycle - outsideStartCycle;
        }
    }
}

void Predictor::outsideCountingEventsStart() {
#ifdef OPEN_COUNTING_EVENT
    getPerfCounts(&startCountingEvent);
#endif
}

void Predictor::outsideCountingEventsStop() {
#ifdef OPEN_COUNTING_EVENT
    getPerfCounts(&stopCountingEvent);

    if(stopCountingEvent.faults > startCountingEvent.faults) {
        countingEvent.faults += stopCountingEvent.faults - startCountingEvent.faults;
    }
    if(stopCountingEvent.cache > startCountingEvent.cache) {
        countingEvent.cache += stopCountingEvent.cache - startCountingEvent.cache;
    }
    if(stopCountingEvent.instructions > startCountingEvent.instructions) {
        countingEvent.instructions += stopCountingEvent.instructions - startCountingEvent.instructions;
    }
#endif
}

void Predictor::threadEnd() {
    threadCycle[ThreadLocalStatus::runningThreadIndex] += outsideCycle;
    threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] += outsideCycle;
    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        threadCycle[ThreadLocalStatus::runningThreadIndex] += functionCycles[allocationType];
        if(replacedFunctionCycles[allocationType] == 0) {
            threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] += functionCycles[allocationType];
        } else {
            threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] += numOfFunctions[allocationType] * replacedFunctionCycles[allocationType];
        }
    }

//    if(threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] < faultedPages * cyclePerPageFault) {
//        threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] = 0;
//    } else {
//        threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] -= faultedPages * cyclePerPageFault;
//    }
#ifdef OPEN_COUNTING_EVENT
    threadCountingEvents[ThreadLocalStatus::runningThreadIndex].add(countingEvent);
#endif

    if(ThreadLocalStatus::runningThreadIndex) {
        lastThreadDepend = false;
        if(outsideCycle * 100 / threadCycle[ThreadLocalStatus::runningThreadIndex] >= 98) {
            lastThreadIndex = ThreadLocalStatus::runningThreadIndex;
            lastThreadDepend = true;
        }
//        fprintf(stderr, "lastThreadDepend = %u at %u\n", lastThreadDepend, lastThreadIndex);
//        fprintf(stderr, "thread end %lu %lu %lu\n",
//                threadCycle[ThreadLocalStatus::runningThreadIndex], threadReplacedCycle[ThreadLocalStatus::runningThreadIndex], outsideCycle);
    }

//    countingEvent.debugPrint();
}

void Predictor::stopSerial() {
    totalCycle += outsideCycle;
    criticalCycle += outsideCycle;
    replacedCriticalCycle += outsideCycle;
    criticalCycleDepend += outsideCycle;
    replacedCriticalCycleDepend += outsideCycle;

    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        criticalCycle += functionCycles[allocationType];
        criticalCycleDepend += functionCycles[allocationType];
        if(replacedFunctionCycles[allocationType] == 0) {
            replacedCriticalCycle += functionCycles[allocationType];
            replacedCriticalCycleDepend += functionCycles[allocationType];
        } else {
            replacedCriticalCycle += numOfFunctions[allocationType] * replacedFunctionCycles[allocationType];
            replacedCriticalCycleDepend += numOfFunctions[allocationType] * replacedFunctionCycles[allocationType];
        }
    }

//    if(replacedCriticalCycle < faultedPages * cyclePerPageFault) {
//        replacedCriticalCycle = 0;
//    } else {
//        replacedCriticalCycle -= faultedPages * cyclePerPageFault;
//    }
//
//    if(replacedCriticalCycleDepend < faultedPages * cyclePerPageFault) {
//        replacedCriticalCycleDepend = 0;
//    } else {
//        replacedCriticalCycleDepend -= faultedPages * cyclePerPageFault;
//    }
#ifdef OPEN_COUNTING_EVENT
    criticalCountingEvent.add(countingEvent);
#endif
//    fprintf(stderr, "***stop serial %lu %lu\n", criticalCycle, replacedCriticalCycle);
//    criticalCountingEvent.debugPrint();
    cleanStageData();
}

void Predictor::stopParallel() {
    uint64_t criticalStageCycle = 0;
    uint64_t criticalReplacedStageCycle = 0;
    uint64_t criticalStageCycleDepend = 0;
    uint64_t criticalReplacedStageCycleDepend = 0;
//    uint64_t numOfActiveThreads = 0;
#ifdef OPEN_COUNTING_EVENT
    PerfReadInfo criticalStageCountingEvent;
    memset(&criticalStageCountingEvent, 0, sizeof(PerfReadInfo));
#endif


//    fprintf(stderr, "***stop parallel %lu %lu %lu %lu\n", criticalCycle, replacedCriticalCycle, criticalCycleDepend, replacedCriticalCycleDepend);

///max
//fprintf(stderr, "lastThreadIndex = %u\n", lastThreadIndex);

    for(unsigned int index = 1; index < ThreadLocalStatus::totalNumOfThread; ++index) {
        if(threadCycle[index] && threadReplacedCycle[index]) {
//            fprintf(stderr, "***cycle = %lu, replacedCycle = %lu\n", threadCycle[index], threadReplacedCycle[index]);
            criticalStageCycle = MAX(criticalStageCycle, threadCycle[index]);
            criticalReplacedStageCycle = MAX(criticalReplacedStageCycle, threadReplacedCycle[index]);
            if(lastThreadDepend && index != lastThreadIndex) {
                    criticalStageCycleDepend = MAX(criticalStageCycleDepend, threadCycle[index]);
                    criticalReplacedStageCycleDepend = MAX(criticalReplacedStageCycleDepend, threadReplacedCycle[index]);
            }
        }
    }

//    fprintf(stderr, "stage %lu %lu %lu\n", criticalStageCycleDepend, criticalReplacedStageCycleDepend, threadCycle[lastThreadIndex]);
//    fprintf(stderr, "***stop parallel %lu %lu %lu %lu\n", criticalCycle, replacedCriticalCycle, criticalCycleDepend, replacedCriticalCycleDepend);


///avg
//    for(unsigned int index = 1; index < ThreadLocalStatus::totalNumOfThread; ++index) {
//        if(threadCycle[index] && threadReplacedCycle[index]) {
//            numOfActiveThreads++;
//            criticalStageCycle += threadCycle[index];
//            criticalReplacedStageCycle += threadReplacedCycle[index];
//        }
//    }
//    criticalStageCycle /= numOfActiveThreads;
//    criticalReplacedStageCycle /= numOfActiveThreads;

    
    for(unsigned short index = 0; index < ThreadLocalStatus::totalNumOfThread; ++index) {
        if(threadCycle[index] && threadReplacedCycle[index]) {
            totalCycle += threadCycle[index];
#ifdef OPEN_COUNTING_EVENT
            criticalCountingEvent.add(threadCountingEvents[index]);
#endif
        }
    }

    criticalCycle += criticalStageCycle;
    replacedCriticalCycle += criticalReplacedStageCycle;
//    criticalCycleDepend += criticalStageCycleDepend + threadCycle[lastThreadIndex];
//    replacedCriticalCycleDepend += criticalReplacedStageCycleDepend + threadReplacedCycle[lastThreadIndex];
    criticalCycleDepend += criticalStageCycleDepend;
    replacedCriticalCycleDepend += criticalReplacedStageCycleDepend;

//    fprintf(stderr, "***stop parallel %lu %lu\n", criticalCycle, replacedCriticalCycle);
//    criticalCountingEvent.debugPrint();
    cleanStageData();
}

void Predictor::printOutput() {
    GlobalStatus::printTitle((char*)"PREDICTION");
    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(replacedFunctionCycles[allocationType] != 0) {
            fprintf(ProgramStatus::outputFile, "replacing cycles in %s %20lu\n", allocationTypeOutputString[allocationType], replacedFunctionCycles[allocationType]);
        }
    }
    fprintf(ProgramStatus::outputFile, "original critical cycle %20lu\n", criticalCycle);
    fprintf(ProgramStatus::outputFile, "predicted critical cycle%20lu\n", replacedCriticalCycle);
    if(lastThreadDepend) {
        fprintf(ProgramStatus::outputFile, "original critical cycle if children threads are depended %20lu\n", criticalCycleDepend);
        fprintf(ProgramStatus::outputFile, "predicted critical cycle if children threads are depended %20lu\n", replacedCriticalCycleDepend);
    }
    if(replacedCriticalCycle) {
        fprintf(ProgramStatus::outputFile, "ratio %3lu%%", criticalCycle*100/replacedCriticalCycle);
        if(lastThreadDepend && replacedCriticalCycleDepend) {
            fprintf(ProgramStatus::outputFile, " ~ %3lu%%\n", criticalCycleDepend*100/replacedCriticalCycleDepend);
        } else {
            fprintf(ProgramStatus::outputFile, "\n");
        }
    } else {
        fprintf(ProgramStatus::outputFile, "ratio 100%%\n");
    }
    if(replacedCriticalCycle && replacedCriticalCycle < criticalCycle) {
        fprintf(ProgramStatus::outputFile, "speed up %3lu%%\n", (criticalCycle-replacedCriticalCycle)*100/replacedCriticalCycle);
    } else {
        fprintf(ProgramStatus::outputFile, "speed up 0%%\n");
    }

    fprintf(ProgramStatus::outputFile, "total cycles               %20lu\n", totalCycle);

#ifdef OPEN_COUNTING_EVENT
    if(totalCycle/1000) {
        fprintf(ProgramStatus::outputFile, "faults                     %20lu    %20lu per 1k cycle\n",
                criticalCountingEvent.faults, criticalCountingEvent.faults/(totalCycle/1000));
        fprintf(ProgramStatus::outputFile, "cache misses               %20lu    %20lu per 1k cycle\n",
                criticalCountingEvent.cache, criticalCountingEvent.cache/(totalCycle/1000));
        fprintf(ProgramStatus::outputFile, "instructions               %20lu    %20lu per 1k cycle\n",
                criticalCountingEvent.instructions, criticalCountingEvent.instructions/(totalCycle/1000));
    }
#endif
}

void Predictor::fopenPredictorInfoFile() {
    char predictorInfoFileName[MAX_FILENAME_LEN];
    strcpy(predictorInfoFileName, "/home/jinzhou/mmprof/predictor.info");
    fprintf(stderr, "Opening prediction info file %s...\n", predictorInfoFileName);
    if ((predictorInfoFile = fopen (predictorInfoFileName, "r")) == NULL) {
        perror("Failed to open prediction info file");
        abort();
    }
}

void Predictor::readFunctionCyclesFromInfo(char*token) {
    if ((strcmp(token, "function_cycles")) == 0) {
        for (uint8_t allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; allocationType++) {
            token = strtok(NULL, " ");
            replacedFunctionCycles[allocationType] = (size_t) atoi(token);
        }
    }
//    replacedFunctionCycles[SERIAL_SMALL_NEW_MALLOC] = 118;
//    replacedFunctionCycles[SERIAL_SMALL_FREE] = 326;
//    replacedFunctionCycles[PARALLEL_SMALL_NEW_MALLOC] = 6919;
//    replacedFunctionCycles[PARALLEL_SMALL_REUSED_MALLOC] = 125;
//    replacedFunctionCycles[PARALLEL_SMALL_FREE] = 92;
}

void Predictor::readMiddleObjectThresholdFromInfo(char*token) {
    if ((strcmp(token, "middle_object_threshold")) == 0) {
        token = strtok(NULL, " ");
        replacedMiddleObjectThreshold = (unsigned short) atoi(token);
    }
}

void Predictor::readLargeObjectThresholdFromInfo(char*token) {
    if ((strcmp(token, "large_object_threshold")) == 0) {
        token = strtok(NULL, " ");
        replacedLargeObjectThreshold = (unsigned int) atoi(token);
    }
}

//void Predictor::readPageFaultCycleFromInfo(char*token) {
//    if ((strcmp(token, "cycle_page_fault")) == 0) {
//        token = strtok(NULL, " ");
////        cyclePerPageFault = (size_t) atoi(token);
//    }
//}


void Predictor::readPredictorInfoFile() {
    size_t bufferSize = 65535;
    char * buffer = (char*)MyMalloc::malloc(bufferSize);

    while (getline(&buffer, &bufferSize, predictorInfoFile) > 0) {
        char *token = strtok(buffer, " ");
        if(token) {
            readFunctionCyclesFromInfo(token);
            readMiddleObjectThresholdFromInfo(token);
            readLargeObjectThresholdFromInfo(token);
//            readPageFaultCycleFromInfo(token);
        }
    }
}

void Predictor::openPredictorInfoFile() {
    fopenPredictorInfoFile();
    readPredictorInfoFile();
    fclose(predictorInfoFile);
}