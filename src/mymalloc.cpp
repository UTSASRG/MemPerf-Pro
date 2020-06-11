#include "mymalloc.h"

ProfilerMemory MyMalloc::profilerMemory;
ProfilerMemory MyMalloc::profilerHashMemory;
thread_local ThreadLocalProfilerMemory MyMalloc::threadLocalProfilerMemory;
