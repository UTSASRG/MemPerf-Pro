//
// Created by 86152 on 2020/5/20.
//

#ifndef MMPROF_PROGRAMSTATUS_H
#define MMPROF_PROGRAMSTATUS_H

#include "definevalues.h"

struct SizeClassSizeAndIndex {
    size_t size;
    size_t classSize;
    unsigned int classSizeIndex;
    void updateValues(size_t size, size_t classSize, unsigned int classSizeIndex) {
        this->size = size;
        this->classSize = classSize;
        this->classSizeIndex = classSizeIndex;
    }
};

class ProgramStatus {
private:
    static bool profilerInitialized;
    static char inputInfoFileName[MAX_FILENAME_LEN];
    static FILE * inputInfoFile;
    static char outputFileName[MAX_FILENAME_LEN];

    static size_t largeObjectThreshold;

    static bool selfMapInitialized;

    static thread_local SizeClassSizeAndIndex cacheForGetClassSizeAndIndex;

    static void getInputInfoFileName();
    static void fopenInputInfoFile();
    static void readAllocatorStyleFromInfo(char*token);
    static void readAllocatorClassSizesFromInfo(char*token);
    static void readLargeObjectThresholdFromInfo(char*token);
    static void readInputInfoFile();
    static void openInputInfoFile();
    static void openOutputFile();

    static void setSelfMapInitializedTrue();

    static void printStackAddr();
    static void printLargeObjectThreshold();

public:

    static FILE * outputFile;
    static void setProfilerInitializedTrue();

    static bool allocatorStyleIsBibop;
    static unsigned int numberOfClassSizes;
    static size_t classSizes[10000];

    static bool profilerNotInitialized();
    static void checkSystemIs64Bits();

    static void initIO();
    static void printOutput();

    static bool selfMapInitializedIsTrue();

    static bool isALargeObject(size_t size);

    SizeClassSizeAndIndex getClassSizeAndIndex(size_t size);
};

#endif //MMPROF_PROGRAMSTATUS_H
