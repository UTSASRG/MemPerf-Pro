#include "predictor.h"

uint64_t Predictor::criticalCycle;
uint64_t Predictor::replacedCriticalCycle;
uint64_t Predictor::criticalCycleDepend;
uint64_t Predictor::replacedCriticalCycleDepend;

uint64_t Predictor::threadCycle[MAX_THREAD_NUMBER];
uint64_t Predictor::threadReplacedCycle[MAX_THREAD_NUMBER];

thread_local unsigned int Predictor::numOfFunctions[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];
thread_local uint64_t Predictor::totalFunctionCycles;
size_t Predictor::replacedFunctionCycles[NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA];

unsigned short Predictor::replacedMiddleObjectThreshold;
unsigned int Predictor::replacedLargeObjectThreshold;

thread_local uint64_t Predictor::outsideStartCycle;
thread_local uint64_t Predictor::outsideStopCycle;
thread_local uint64_t Predictor::outsideCycle;
thread_local uint64_t Predictor::outsideCycleMinus;


FILE * Predictor::predictorInfoFile;

bool Predictor::lastThreadDepend;
unsigned short Predictor::lastThreadIndex;


ObjectSizeType Predictor::getObjectSizeTypeForPrediction(unsigned int size) {
    if(size > replacedLargeObjectThreshold) {
        return LARGE;
    }
    if(ProgramStatus::hasMiddleObjectThreshold() && size >= replacedMiddleObjectThreshold) {
        return MEDIUM;
    }
    return SMALL;
}

void Predictor::globalInit() {
    openPredictorInfoFile();
    criticalCycle = 0;
    replacedCriticalCycle = 0;
    criticalCycleDepend = 0;
    replacedCriticalCycleDepend = 0;

    memset(numOfFunctions, 0, sizeof(unsigned int) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);
    totalFunctionCycles = 0;

    outsideCycle = 0;
//    outsideStartCycle = 0;
//    outsideStopCycle = 0;
}

void Predictor::threadInit() {

    memset(numOfFunctions, 0, sizeof(unsigned int) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);
    totalFunctionCycles = 0;

    outsideCycle = 0;
//    outsideStartCycle = 0;
//    outsideStopCycle = 0;
}

void Predictor::cleanStageData() {
    memset(threadCycle, 0, sizeof(uint64_t) * ThreadLocalStatus::totalNumOfThread);
    memset(threadReplacedCycle, 0, sizeof(uint64_t) * ThreadLocalStatus::totalNumOfThread);

    memset(numOfFunctions, 0, sizeof(unsigned int) * NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA);
    totalFunctionCycles = 0;

    outsideCycle = 0;

}

void Predictor::subOutsideCycle(uint64_t cycles) {
    outsideCycleMinus += cycles;
}

void Predictor::outsideCycleStart() {
    outsideStartCycle = rdtscp();
}

void Predictor::outsideCyclesStop() {
    outsideStopCycle = rdtscp();
    if(outsideStopCycle > outsideStartCycle && outsideStartCycle) {
        outsideCycle += outsideStopCycle - outsideStartCycle;
//        fprintf(stderr, "outsideCycle += %lu, %lu - %lu\n", outsideStopCycle - outsideStartCycle, outsideStopCycle, outsideStartCycle);
    }
}

void Predictor::threadEnd() {
    threadCycle[ThreadLocalStatus::runningThreadIndex] += outsideCycle;
    threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] += outsideCycle;
    threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] -= totalFunctionCycles;
    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(replacedFunctionCycles[allocationType]) {
            threadReplacedCycle[ThreadLocalStatus::runningThreadIndex] += (numOfFunctions[allocationType] * replacedFunctionCycles[allocationType]);
        }
    }

//    fprintf(stderr, "thread ends: %lu %lu %lu\n", threadCycle[ThreadLocalStatus::runningThreadIndex], threadReplacedCycle[ThreadLocalStatus::runningThreadIndex], totalFunctionCycles);

    ///change this part
    if(ThreadLocalStatus::runningThreadIndex) {
        lastThreadDepend = false;
        if(totalFunctionCycles * 100 / threadCycle[ThreadLocalStatus::runningThreadIndex] < 2) {
            lastThreadIndex = ThreadLocalStatus::runningThreadIndex;
            lastThreadDepend = true;
        }
    }

}

void Predictor::stopSerial() {
    criticalCycle += outsideCycle;
    criticalCycleDepend += outsideCycle;
    replacedCriticalCycle += outsideCycle;
    replacedCriticalCycle -= totalFunctionCycles;
    replacedCriticalCycleDepend += outsideCycle;
    replacedCriticalCycleDepend -= totalFunctionCycles;

    for(unsigned int allocationType = 0; allocationType < NUM_OF_ALLOCATIONTYPEFOROUTPUTDATA; ++allocationType) {
        if(replacedFunctionCycles[allocationType]) {
            replacedCriticalCycle += (numOfFunctions[allocationType] * replacedFunctionCycles[allocationType]);
            replacedCriticalCycleDepend += (numOfFunctions[allocationType] * replacedFunctionCycles[allocationType]);
        }
    }
//    fprintf(stderr, "stop serial: %lu %lu\n", criticalCycle, replacedCriticalCycle);
    cleanStageData();
}

void Predictor::stopParallel() {
    uint64_t criticalStageCycle = 0;
    uint64_t criticalReplacedStageCycle = 0;
    uint64_t criticalStageCycleDepend = 0;
    uint64_t criticalReplacedStageCycleDepend = 0;


    for(unsigned int index = 1; index < ThreadLocalStatus::totalNumOfThread; ++index) {
        if(threadCycle[index] && threadReplacedCycle[index]) {
            criticalStageCycle = MAX(criticalStageCycle, threadCycle[index]);
            criticalReplacedStageCycle = MAX(criticalReplacedStageCycle, threadReplacedCycle[index]);
            if(lastThreadDepend && index != lastThreadIndex) {
                criticalStageCycleDepend = MAX(criticalStageCycleDepend, threadCycle[index]);
                criticalReplacedStageCycleDepend = MAX(criticalReplacedStageCycleDepend, threadReplacedCycle[index]);
            }
        }
    }

    criticalCycle += criticalStageCycle;
    criticalCycleDepend += criticalStageCycleDepend;
    replacedCriticalCycle += criticalReplacedStageCycle;
    replacedCriticalCycleDepend += criticalReplacedStageCycleDepend;

//    fprintf(stderr, "stop parallel: %lu %lu %lu %lu\n", criticalCycle, replacedCriticalCycle, criticalStageCycle, criticalReplacedStageCycle);

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

    if(replacedCriticalCycle && replacedCriticalCycle < criticalCycle) {
        fprintf(stderr, "%s: SPEED UP %3lu%%\n", ProgramStatus::outputFileName, (criticalCycle-replacedCriticalCycle)*100/replacedCriticalCycle);
    } else {
        fprintf(stderr, "%s: SPEED UP 0%%\n", ProgramStatus::outputFileName);
    }

}

void Predictor::fopenPredictorInfoFile() {
    char predictorInfoFileName[MAX_FILENAME_LEN];
    strcpy(predictorInfoFileName, "/home/jinzhou/mmprof/predictor_naive.info");
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


void Predictor::readPredictorInfoFile() {
    size_t bufferSize = 65535;
    char * buffer = (char*)MyMalloc::malloc(bufferSize);

    while (getline(&buffer, &bufferSize, predictorInfoFile) > 0) {
        char *token = strtok(buffer, " ");
        if(token) {
            readFunctionCyclesFromInfo(token);
            readMiddleObjectThresholdFromInfo(token);
            readLargeObjectThresholdFromInfo(token);
        }
    }
}

void Predictor::openPredictorInfoFile() {
    fopenPredictorInfoFile();
    readPredictorInfoFile();
    fclose(predictorInfoFile);
}