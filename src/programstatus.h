//
// Created by 86152 on 2020/5/20.
//

#ifndef MMPROF_PROGRAMSTATUS_H
#define MMPROF_PROGRAMSTATUS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mymalloc.h"
#include "string.h"
#include "structs.h"

class ProgramStatus {
private:
    static bool profilerInitialized;
    static bool beginConclusion;

    static char inputInfoFileName[MAX_FILENAME_LEN];
    static FILE * inputInfoFile;
    static char outputFileName[MAX_FILENAME_LEN];

    static size_t largeObjectThreshold;

    static thread_local struct SizeClassSizeAndIndex cacheForGetClassSizeAndIndex;

    static void getInputInfoFileName();
    static void fopenInputInfoFile();
    static void readAllocatorStyleFromInfo(char*token);
    static void readAllocatorClassSizesFromInfo(char*token);
    static void readLargeObjectThresholdFromInfo(char*token);
    static void readInputInfoFile();
    static void openInputInfoFile();
    static void openOutputFile();

    static void printStackAddr();
    static void printLargeObjectThreshold();

public:

    static FILE * outputFile;
    static bool allocatorStyleIsBibop;

    static unsigned int numberOfClassSizes;
    static size_t classSizes[10000];

    static void setProfilerInitializedTrue();
    static void setBeginConclusionTrue();

    static bool profilerNotInitialized();
    static bool conclusionHasStarted();
    static void checkSystemIs64Bits();

    static void initIO();
    static void printOutput();

    static bool isALargeObject(size_t size);

    static struct SizeClassSizeAndIndex getClassSizeAndIndex(size_t size);
};

#endif //MMPROF_PROGRAMSTATUS_H
