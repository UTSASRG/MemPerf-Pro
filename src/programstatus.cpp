//
// Created by 86152 on 2020/5/20.
//
#include <stdio.h>
#include <stdlib.h>
#include "programstatus.h"
#include "definevalues.h"
#include "selfmap.hh"
//#include "recordscale.hh"
#include "mymalloc.h"
#include "string.h"

bool ProgramStatus::profilerInitialized = false;
bool ProgramStatus::selfMapInitialized = false;

FILE * ProgramStatus::inputInfoFile;
FILE * ProgramStatus::outputFile;
char ProgramStatus::inputInfoFileName[MAX_FILENAME_LEN];
char ProgramStatus::outputFileName[MAX_FILENAME_LEN];

bool ProgramStatus::allocatorStyleIsBibop;
size_t ProgramStatus::largeObjectThreshold;
size_t ProgramStatus::classSizes[10000];
unsigned int ProgramStatus::numberOfClassSizes;

void ProgramStatus::setProfilerInitializedTrue() {
    profilerInitialized = true;
}
bool ProgramStatus::profilerNotInitialized() {
    return !profilerInitialized;
}

void ProgramStatus::setSelfMapInitializedTrue() {
    selfMapInitialized = true;
}
bool ProgramStatus::selfMapInitializedIsTrue() {
    return selfMapInitialized;
}

// Ensure we are operating on a system using 64-bit pointers.
void ProgramStatus::checkSystemIs64Bits() {
    if(sizeof(void *) != EIGHT_BYTES) {
        fprintf(stderr, "error: unsupported pointer size: %zu\n", sizeof(void *));
        abort();
    }
}

void ProgramStatus::getInputInfoFileName() {
    char allocator_name[100];
    selfmap::getInstance().getTextRegions(allocator_name);
    setSelfMapInitializedTrue();
    char* end_path = strrchr(allocator_name, '/');
    char* end_name = strchr(end_path, '.') - 1;
    char* start_name = end_path + 1;
    uint64_t name_bytes = (uint64_t)end_name - (uint64_t)end_path;
    uint64_t path_bytes = (uint64_t)start_name - (uint64_t)allocator_name;
    short ext_bytes = 6;
    memcpy(inputInfoFileName, allocator_name, path_bytes);
    memcpy((inputInfoFileName+path_bytes), start_name, name_bytes);
    snprintf ((inputInfoFileName+path_bytes+name_bytes), ext_bytes, ".info");
}

void ProgramStatus::fopenInputInfoFile() {
    fprintf(stderr, "Opening allocator info file %s...\n", inputInfoFileName);
    if ((inputInfoFile = fopen (inputInfoFileName, "r")) == NULL) {
        perror("Failed to open allocator info file");
        abort();
    }
}

void ProgramStatus::readAllocatorStyleFromInfo(char * token) {
    if ((strcmp(token, "style")) == 0) {

        token = strtok(NULL, " ");

        if ((strcmp(token, "bibop\n")) == 0) {
            allocatorStyleIsBibop = true;
        } else {
            allocatorStyleIsBibop = false;
        }
    }
}

void ProgramStatus::readAllocatorClassSizesFromInfo(char * token) {
    if ((strcmp(token, "class_sizes")) == 0) {

        token = strtok(NULL, " ");
        numberOfClassSizes = atoi(token);

        if(allocatorStyleIsBibop) {
            for (int i = 0; i < numberOfClassSizes; i++) {
                token = strtok(NULL, " ");
                classSizes[i] = (size_t) atoi(token);
            }
        } else {
            for (int i = 0; i < numberOfClassSizes; i++) {
                classSizes[i] = (size_t) 24 + 16 * i;
            }
        }
    }
}

void ProgramStatus::readLargeObjectThresholdFromInfo(char * token) {
    if ((strcmp(token, "large_object_threshold")) == 0) {
        token = strtok(NULL, " ");
        largeObjectThreshold = (size_t) atoi(token);
    }
}

void ProgramStatus::readInputInfoFile() {

    size_t bufferSize = 1024;
    char * buffer = (char*)MyMalloc::malloc(bufferSize);

    while (getline(&buffer, &bufferSize, ProgramStatus::inputInfoFile) > 0) {
        char *token = strtok(buffer, " ");
        readAllocatorStyleFromInfo(token);
        readAllocatorClassSizesFromInfo(token);
        readLargeObjectThresholdFromInfo(token);
    }
}

void ProgramStatus::openInputInfoFile() {
    ProgramStatus::getInputInfoFileName();
    ProgramStatus::fopenInputInfoFile();
    ProgramStatus::readInputInfoFile();
}

void ProgramStatus::openOutputFile() {
//	snprintf(ProgramStatus::outputInfoFileName, MAX_FILENAME_LEN, "%s_libmallocprof_%d_main_thread.txt",
//			program_invocation_name, getpid());
    snprintf(ProgramStatus::outputFileName, MAX_FILENAME_LEN, "/home/jinzhou/parsec/records_t/%s_libmallocprof_%d_main_thread.txt",
             program_invocation_name, getpid());
    fprintf(stderr, "%s\n", ProgramStatus::outputFileName);

    ProgramStatus::outputFile = fopen(ProgramStatus::outputFileName, "w");
    if(ProgramStatus::outputFile == NULL) {
        perror("error: unable to open output file to write");
        abort();
    }
}

void ProgramStatus::initIO() {
    ProgramStatus::openInputInfoFile();
    ProgramStatus::openOutputFile();
}

void ProgramStatus::printStackAddr() {
    extern void * __libc_stack_end;
    fprintf(outputFile, ">>> stack start @ %p, stack end @ %p\n", (char *)__builtin_frame_address(0), (char *)__libc_stack_end);
    fprintf(outputFile, ">>> program break @ %p\n", RealX::sbrk(0));
}

bool ProgramStatus::isALargeObject(size_t size) {
    return size > largeObjectThreshold;
}

SizeClassSizeAndIndex ProgramStatus::getClassSizeAndIndex(size_t size) {

    if(size == cacheForGetClassSizeAndIndex.size) {
        return cacheForGetClassSizeAndIndex;
    }

    size_t classSize;
    unsigned int classSizeIndex;

    if(size > largeObjectThreshold) {
        classSize = size;
        classSizeIndex = numberOfClassSizes - 1;
        return SizeClassSizeAndIndex{size, classSize, classSizeIndex};
    }

    if(allocatorStyleIsBibop) {
        for (int index = 0; index < numberOfClassSizes; index++) {
            if (size <= classSizes[index]) {
                classSize = classSizes[index];
                classSizeIndex = index;
                break;
            }
        }
    }
    else {
        if(allocData->size <= 24) {
            classSize = 24;
            classSizeIndex = 0;
        } else {
            classSizeIndex = (allocData->size - 24) / 16 + 1;
            classSize = class_sizes[allocData->classSizeIndex];
        }
    }
    cacheForGetClassSizeAndIndex.updateValues(size, classSize, classSizeIndex);
    return cacheForGetClassSizeAndIndex;
}