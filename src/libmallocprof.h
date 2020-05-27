#ifndef __LIBMALLOCPROF_H__
#define __LIBMALLOCPROF_H__

#include "memsample.h"
#include <signal.h>
#include <limits.h>
#include "definevalues.h"

#define MEM_SYS_START LOCK_TYPE_TOTAL

typedef struct {
  ulong calls[4] = {0, 0, 0, 0};
  ulong cycles[4] = {0, 0, 0, 0};
  ulong new_calls = 0;
  ulong ffl_calls = 0;
  ulong new_cycles = 0;
  ulong ffl_cycles = 0;
} PerPrimitiveData;

//Structure for perthread contention
typedef struct {
	pid_t tid = 0;
 
  // Instead for different types. Let's use an array here. 
  PerPrimitiveData pmdata[LOCK_TYPE_TOTAL];

  ulong lock_counter = 0;
  uint64_t critical_section_start = 0;
  ulong critical_section_counter = 0;
  uint64_t critical_section_duration = 0;
} __attribute__((__aligned__(CACHELINE_SIZE))) ThreadContention;

typedef struct {
	unsigned long numAccesses = 0;
	unsigned long pageUtilTotal = 0;
	unsigned long cacheUtilTotal = 0;
} PerfAppFriendly;


inline bool isAllocatorInCallStack();
size_t getClassSizeFor(size_t size);
int num_used_pages(uintptr_t vstart, uintptr_t vend);
void getAddressUsage(size_t size, uint64_t address, uint64_t cycles);

void getMappingsUsage(size_t size, uint64_t address, size_t classSize);
void getMetadata(size_t classSize);

void getPerfCounts(PerfReadInfo*);

void globalizeTAD();

void readAllocatorFile();
void writeAllocData ();
void writeContention ();
void writeMappings();

void writeThreadContention();
void writeThreadMaps();

void initGlobalCSM();

void updateGlobalFriendlinessData();
void calcAppFriendliness();
const char * LockTypeToString(LockType type);

inline double safeDivisor(ulong divisor) {
	return (!divisor) ? 1.0 : (double)divisor;
}

#include "shadowmemory.hh"
#endif /* end of include guard: __LIBMALLOCPROF_H__ */
