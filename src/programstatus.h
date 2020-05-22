//
// Created by 86152 on 2020/5/20.
//

#ifndef MMPROF_PROGRAMSTATUS_H
#define MMPROF_PROGRAMSTATUS_H

#include "definevalues.h"

class ProgramStatus {
private:
    static bool profilerInitialized;
    static char inputInfoFileName[MAX_FILENAME_LEN];
    static FILE * inputInfoFile;
    static char outputFileName[MAX_FILENAME_LEN];

    static bool allocatorStyleIsBibop;
    static unsigned int numberOfClassSizes;
    static size_t classSizes[10000];
    static size_t largeObjectThreshold;

    static bool selfMapInitialized;

    static void getInputInfoFileName();
    static void fopenInputInfoFile();
    static void readAllocatorStyleFromInfo(char*token);
    static void readAllocatorClassSizesFromInfo(char*token);
    static void readLargeObjectThresholdFromInfo(char*token);
    static void readInputInfoFile();
    static void openInputInfoFile();
    static void openOutputFile();

    static void setSelfMapInitializedTrue();

public:

    static FILE * outputFile;
    static void setProfilerInitializedTrue();

    static bool profilerInitializedIsTrue();
    static void checkSystemIs64Bits();

    static void initIO();
    static void printStackAddr();

    static bool selfMapInitializedIsTrue();
};

#endif //MMPROF_PROGRAMSTATUS_H
