#include "programstatus.h"

thread_local bool ProgramStatus::profilerInitialized;
bool ProgramStatus::beginConclusion;
char ProgramStatus::inputInfoFileName[MAX_FILENAME_LEN];
FILE * ProgramStatus::inputInfoFile;
char ProgramStatus::outputFileName[MAX_FILENAME_LEN];
size_t ProgramStatus::largeObjectThreshold;
thread_local struct SizeClassSizeAndIndex ProgramStatus::cacheForGetClassSizeAndIndex;

FILE * ProgramStatus::outputFile;
bool ProgramStatus::allocatorStyleIsBibop;
unsigned int ProgramStatus::numberOfClassSizes;
size_t ProgramStatus::classSizes[10000];


void ProgramStatus::setProfilerInitializedTrue() {
    profilerInitialized = true;
}
void ProgramStatus::setBeginConclusionTrue() {
    beginConclusion = true;
}
bool ProgramStatus::profilerNotInitialized() {
    return !profilerInitialized;
}
bool ProgramStatus::conclusionHasStarted() {
    return beginConclusion;
}

// Ensure we are operating on a system using 64-bit pointers.
void ProgramStatus::checkSystemIs64Bits() {
    if(sizeof(void *) != EIGHT_BYTES) {
        fprintf(stderr, "error: unsupported pointer size: %zu\n", sizeof(void *));
        abort();
    }
}

void ProgramStatus::getInputInfoFileName(char * runningApplicationName) {
    char * runningAllocatorName = strrchr(runningApplicationName, '-')+1;
    if(strcmp(runningAllocatorName, "libc228") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/Memoryallocators/libc-2.28/libmalloc.info");
    } else if(strcmp(runningAllocatorName, "libc221") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/Memoryallocators/libc-2.21/libmalloc.info");
    } else if(strcmp(runningAllocatorName, "hoard") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/Memoryallocators/Hoard/src/libhoard.info");
    } else if(strcmp(runningAllocatorName, "jemalloc") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/Memoryallocators/jemalloc/libjemalloc.info");
    } else if(strcmp(runningAllocatorName, "tcmalloc") == 0) {
        strcpy(inputInfoFileName, "/usr/local/lib/libtcmalloc.info");
    } else if(strcmp(runningAllocatorName, "dieharder") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/Memoryallocators/DieHard/src/libdieharder.info");
    } else if(strcmp(runningAllocatorName, "omalloc") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/Memoryallocators/OpenBSD-6.0-mus/libomalloc.info");
    } else {
        fprintf(stderr, "Info File Location Unknown\n");
        abort();
    }
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
            for (unsigned int i = 0; i < numberOfClassSizes; i++) {
                token = strtok(NULL, " ");
                classSizes[i] = (size_t) atoi(token);
            }
        } else {
            for (unsigned int i = 0; i < numberOfClassSizes; i++) {
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

    size_t bufferSize = 65535;
    char * buffer = (char*)MyMalloc::malloc(bufferSize);

    while (getline(&buffer, &bufferSize, ProgramStatus::inputInfoFile) > 0) {
        char *token = strtok(buffer, " ");
        if(token) {
            readAllocatorStyleFromInfo(token);
            readAllocatorClassSizesFromInfo(token);
            readLargeObjectThresholdFromInfo(token);
        }
    }
}

void ProgramStatus::openInputInfoFile(char * runningApplicationName) {
    ProgramStatus::getInputInfoFileName(runningApplicationName);
    ProgramStatus::fopenInputInfoFile();
    ProgramStatus::readInputInfoFile();
}

void ProgramStatus::openOutputFile() {
    extern char * program_invocation_name;
	snprintf(ProgramStatus::outputFileName, MAX_FILENAME_LEN, "%s_libmallocprof_%d_main_thread.txt", program_invocation_name, getpid());
//    snprintf(ProgramStatus::outputFileName, MAX_FILENAME_LEN, "/home/jinzhou/parsec/records/%s_libmallocprof_%d_main_thread.txt", program_invocation_name, getpid());
    fprintf(stderr, "%s\n", ProgramStatus::outputFileName);

    ProgramStatus::outputFile = fopen(ProgramStatus::outputFileName, "w");
    if(ProgramStatus::outputFile == NULL) {
        perror("error: unable to open output file to write");
        abort();
    }
}

void ProgramStatus::initIO(char * runningApplicationName) {
    ProgramStatus::openInputInfoFile(runningApplicationName);
    ProgramStatus::openOutputFile();
}

void ProgramStatus::printLargeObjectThreshold() {
    fprintf(outputFile, ">>> large_object_threshold                  %20zu\n", largeObjectThreshold);
}

void ProgramStatus::printOutput() {
    printLargeObjectThreshold();
}

bool ProgramStatus::isALargeObject(size_t size) {
    return size > largeObjectThreshold;
}

SizeClassSizeAndIndex ProgramStatus::getClassSizeAndIndex(size_t size) {

    if(size == cacheForGetClassSizeAndIndex.size) {
        return cacheForGetClassSizeAndIndex;
    }

    size_t classSize = 0;
    unsigned int classSizeIndex = 0;

    if(size > largeObjectThreshold) {
        classSize = size;
        classSizeIndex = numberOfClassSizes - 1;
        return SizeClassSizeAndIndex{size, classSize, classSizeIndex};
    }

    if(allocatorStyleIsBibop) {
        for (unsigned int index = 0; index < numberOfClassSizes; index++) {
            if (size <= classSizes[index]) {
                classSize = classSizes[index];
                classSizeIndex = index;
                break;
            }
        }
    }
    else {
        if(size <= 24) {
            classSize = 24;
            classSizeIndex = 0;
        } else {
            classSizeIndex = (size - 24) / 16 + 1;
            classSize = classSizes[classSizeIndex];
        }
    }
    cacheForGetClassSizeAndIndex.updateValues(size, classSize, classSizeIndex);
    return cacheForGetClassSizeAndIndex;
}