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
    static thread_local bool profilerInitialized;
    static bool beginConclusion;

    static char matrixFileName[MAX_FILENAME_LEN];
    static char inputInfoFileName[MAX_FILENAME_LEN];
    static FILE * inputInfoFile;

    static unsigned short middleObjectThreshold;
    static unsigned int largeObjectThreshold;
    static unsigned short largeObjectAlignment;

    static thread_local struct SizeClassSizeAndIndex cacheForGetClassSizeAndIndex;

    static void openMatrixFile();
    static void getInputInfoFileName(char * runningApplicationName);
    static void fopenInputInfoFile();
    static void readAllocatorStyleFromInfo(char*token);
    static void readAllocatorClassSizesFromInfo(char*token);
    static void readMiddleObjectThresholdFromInfo(char*token);
    static void readLargeObjectThresholdFromInfo(char*token);
    static void readLargeObjectAlignmentFromInfo(char*token);
    static void readInputInfoFile();
    static void openInputInfoFile(char * runningApplicationName);
    static void openOutputFile();
    static void printLargeObjectThreshold();

public:

    static char programName[256];
    static bool matrixFileOpened;
    static FILE * matrixFile;
    static FILE * outputFile;
    static char outputFileName[MAX_FILENAME_LEN];
    static bool allocatorStyleIsBibop;

    static unsigned short numberOfClassSizes;
    static unsigned int classSizes[8270];

    static bool useHugePage;

    static void setProfilerInitializedTrue();
    static void setBeginConclusionTrue();
    static void setThreadInitializedTrue();

    static bool profilerNotInitialized();
    static bool conclusionHasStarted();

    static void initIO(char * runningApplicationName);
    static void printOutput();

    static bool hasMiddleObjectThreshold();
    static ObjectSizeType getObjectSizeType(unsigned int size);

    static struct SizeClassSizeAndIndex getClassSizeAndIndex(unsigned int size);

};

#endif //MMPROF_PROGRAMSTATUS_H
