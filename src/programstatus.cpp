#include "programstatus.h"

thread_local bool ProgramStatus::profilerInitialized;
bool ProgramStatus::beginConclusion;
char ProgramStatus::matrixFileName[MAX_FILENAME_LEN];
char ProgramStatus::inputInfoFileName[MAX_FILENAME_LEN];
FILE * ProgramStatus::inputInfoFile;
char ProgramStatus::outputFileName[MAX_FILENAME_LEN];
unsigned short ProgramStatus::middleObjectThreshold;
unsigned int ProgramStatus::largeObjectThreshold;
unsigned short ProgramStatus::largeObjectAlignment;
thread_local struct SizeClassSizeAndIndex ProgramStatus::cacheForGetClassSizeAndIndex;

char ProgramStatus::programName[256];
//bool ProgramStatus::matrixFileOpened;
//FILE * ProgramStatus::matrixFile;
FILE * ProgramStatus::outputFile;
bool ProgramStatus::allocatorStyleIsBibop;
unsigned short ProgramStatus::numberOfClassSizes;
unsigned int ProgramStatus::classSizes[8270];


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

//void ProgramStatus::openMatrixFile() {
//    extern char * program_invocation_name;
////    snprintf(matrixFileName, MAX_FILENAME_LEN, "matrix.txt");
//    snprintf(matrixFileName, MAX_FILENAME_LEN, "/home/jinzhou/parsec/matrix/count.txt");
//    fprintf(stderr, "%s\n", matrixFileName);
//    matrixFile = fopen(matrixFileName, "a+");
//    if(matrixFile == nullptr) {
//        perror("error: unable to open matrix file to write");
//        abort();
//    }
//    matrixFileOpened = true;
//    fprintf(matrixFile, "%s ", program_invocation_name);
//}

void ProgramStatus::getInputInfoFileName(char * runningApplicationName) {
    char * runningAllocatorName = strrchr(runningApplicationName, '-')+1;
    if(strcmp(runningAllocatorName, "libc228") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/mmprof/info/libc228.info");
    } else if(strcmp(runningAllocatorName, "libc221") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/mmprof/info/libc221.info");
    } else if(strcmp(runningAllocatorName, "hoard") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/mmprof/info/libhoard.info");
    } else if(strcmp(runningAllocatorName, "jemalloc") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/mmprof/info/libjemalloc.info");
    } else if(strcmp(runningAllocatorName, "tcmalloc") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/mmprof/info/libtcmalloc.info");
    } else if(strcmp(runningAllocatorName, "dieharder") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/mmprof/info/libdieharder.info");
    } else if(strcmp(runningAllocatorName, "omalloc") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/mmprof/info/libomalloc.info");
    } else if (strcmp(runningAllocatorName, "numalloc") == 0) {
        strcpy(inputInfoFileName, "/home/jinzhou/mmprof/info/libnumalloc.info");
    } else {
        fprintf(stderr, "Info File Location Unknown\n");
        abort();
    }

//    if(ProgramStatus::matrixFileOpened) {
//        fprintf(matrixFile, "%s ", runningAllocatorName);
//    }

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
            for (unsigned short i = 0; i < numberOfClassSizes; i++) {
                token = strtok(NULL, " ");
                classSizes[i] = (unsigned int) atoi(token);
            }
        } else {
            classSizes[0] = 24;
            for (unsigned short i = 1; i < numberOfClassSizes; i++) {
                classSizes[i] = classSizes[i-1] + 16;
            }
        }
    }
    numberOfClassSizes++;
}

void ProgramStatus::readMiddleObjectThresholdFromInfo(char *token) {
    if ((strcmp(token, "middle_object_threshold")) == 0) {
        token = strtok(NULL, " ");
        middleObjectThreshold = (unsigned short) atoi(token);
    }
}

void ProgramStatus::readLargeObjectThresholdFromInfo(char * token) {
    if ((strcmp(token, "large_object_threshold")) == 0) {
        token = strtok(NULL, " ");
        largeObjectThreshold = (unsigned int) atoi(token);
    }
}

void ProgramStatus::readLargeObjectAlignmentFromInfo(char *token) {
    if ((strcmp(token, "large_object_alignment")) == 0) {
        token = strtok(NULL, " ");
        largeObjectAlignment = (unsigned short) atoi(token);
    }
}


void ProgramStatus::readInputInfoFile() {

    size_t bufferSize = 53248;
//    char * buffer = (char*)MyMalloc::malloc(bufferSize);
    char * buffer = (char*)RealX::mmap(NULL, bufferSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    while (getline(&buffer, &bufferSize, ProgramStatus::inputInfoFile) > 0) {
        char *token = strtok(buffer, " ");
        if(token) {
            readAllocatorStyleFromInfo(token);
            readMiddleObjectThresholdFromInfo(token);
            readAllocatorClassSizesFromInfo(token);
            readLargeObjectThresholdFromInfo(token);
            readLargeObjectAlignmentFromInfo(token);
        }
    }

    munmap(buffer, bufferSize);
}

void ProgramStatus::openInputInfoFile(char * runningApplicationName) {
    ProgramStatus::getInputInfoFileName(runningApplicationName);
    ProgramStatus::fopenInputInfoFile();
    ProgramStatus::readInputInfoFile();
    fclose(inputInfoFile);
}

void ProgramStatus::openOutputFile() {
    extern char * program_invocation_name;
//	snprintf(outputFileName, MAX_FILENAME_LEN, "%s_libmallocprof_%d_main_thread.txt", program_invocation_name, getpid());
    snprintf(outputFileName, MAX_FILENAME_LEN, "/home/jinzhou/parsec/records/%s_libmallocprof_%d_main_thread.txt", program_invocation_name, getpid());
    fprintf(stderr, "%s\n", outputFileName);
    outputFile = fopen(outputFileName, "w");
    if(outputFile == nullptr) {
        perror("error: unable to open output file to write");
        abort();
    }

}

void ProgramStatus::initIO(char * runningApplicationName) {
    strcpy(programName, runningApplicationName);
//    ProgramStatus::openMatrixFile();
    ProgramStatus::openInputInfoFile(runningApplicationName);
    ProgramStatus::openOutputFile();
}

void ProgramStatus::printLargeObjectThreshold() {
    fprintf(outputFile, ">>> large_object_threshold                  %20u\n", largeObjectThreshold);
}

void ProgramStatus::printOutput() {
    printLargeObjectThreshold();
}

bool ProgramStatus::hasMiddleObjectThreshold() {
    return (0<middleObjectThreshold) && (middleObjectThreshold<largeObjectThreshold);
}

ObjectSizeType ProgramStatus::getObjectSizeType(unsigned int size) {
    if(size > largeObjectThreshold) {
        return LARGE;
    }
    if(hasMiddleObjectThreshold() && size >=  middleObjectThreshold) {
        return MEDIUM;
    }
    return SMALL;
}

inline size_t alignup(size_t size, size_t alignto) {
    return (size % alignto == 0) ? size : ((size + (alignto - 1)) & ~(alignto - 1));
}

SizeClassSizeAndIndex ProgramStatus::getClassSizeAndIndex(unsigned int size) {

    if(size == cacheForGetClassSizeAndIndex.size) {
        return cacheForGetClassSizeAndIndex;
    }

    unsigned int classSize = 0;
    unsigned short classSizeIndex = 0;

    if(size > largeObjectThreshold) {
        classSize = (size/largeObjectAlignment)*largeObjectAlignment + ((bool)size%largeObjectAlignment)*largeObjectAlignment;
        classSizeIndex = numberOfClassSizes-1;
        return SizeClassSizeAndIndex{classSizeIndex, size, classSize};
    }

    if(allocatorStyleIsBibop) {
        for (unsigned short index = 0; index < numberOfClassSizes-1; index++) {
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
