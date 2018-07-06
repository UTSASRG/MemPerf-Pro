/*
 * @file   selfmap.h
 * @brief  Analyze the self mapping.
 */

#include "selfmap.hh"

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

//#include "xdefines.hh"

#define MAX_BUF_SIZE 4096
#define CALLSITE_MAXIMUM_LENGTH 10

// Normally, callstack only saves next instruction address.
// To get current callstack, we should substract 1 here.
// Then addr2line can figure out which instruction correctly
#define PREV_INSTRUCTION_OFFSET 1

// Print out the code information about an eipaddress
// Also try to print out stack trace of given pcaddr.
void selfmap::printCallStack() {
  void* array[256];
  int frames;

  // get void*'s for all entries on the stack
  frames = backtrace(array, 256);
  // Print out the source code information if it is a overflow site.
  selfmap::getInstance().printCallStack(frames, array);
}

// Calling system involves a lot of irrevocable system calls.
void selfmap::printCallStack(int frames, void** array) {
  char buf[256];
  //  fprintf(stderr, "printCallStack(%d, %p)\n", depth, array);
  for(int i = 0; i < frames; i++) {
    void* addr = (void*)((unsigned long)array[i] - PREV_INSTRUCTION_OFFSET);

    // Print out lines if the callsite is from the application or libmemperf.so
    if(isCurrentLibrary((void *)addr)) {
      addr = (void *)((intptr_t)addr - (intptr_t)_mallocProfTextStart);
      sprintf(buf, "addr2line -e %s %p", _currentLibrary.c_str(), addr); 
    }
    else if(isAllocator((void *)addr)) {
      addr = (void *)((intptr_t)addr - (intptr_t)_allocTextStart);
      sprintf(buf, "addr2line -e %s %p", _allocLibrary.c_str(), addr);
    } 
    else {
      sprintf(buf, "addr2line -a -i -e %s %p", _main_exe.c_str(), addr);
    }
    system(buf);
  }
}
// Print out the code information about an eipaddress
// Also try to print out stack trace of given pcaddr.
int selfmap::getCallStack(void** array) {
  int size;
  // get void*'s for all entries on the stack
  size = backtrace(array, CALLSITE_MAXIMUM_LENGTH);

  return size;
}
