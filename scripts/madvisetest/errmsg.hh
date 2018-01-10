#ifndef ERRMSG_H
#define ERRMSG_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <execinfo.h>
#include "xdefines.hh"
#include "log.hh"

#define PREV_INSTRUCTION_OFFSET 1
#define FILENAMELENGTH 256

void printCallStack(){
  void* array[256];
  int frames = backtrace(array, 256);

  char main_exe[FILENAMELENGTH];
  readlink("/proc/self/exe", main_exe, FILENAMELENGTH);

#ifdef CUSTOMIZED_STACK
  int threadIndex = getThreadIndex(&frames);
#else
  int threadIndex = getThreadIndex();
#endif

  char buf[256];
  for(int i = 0; i < frames; i++) {
    void* addr = (void*)((unsigned long)array[i] - PREV_INSTRUCTION_OFFSET);
    PRINT("\tThread %d: callstack frame %d: %p\t", threadIndex, i, addr);
    sprintf(buf, "addr2line -a -i -e %s %p", main_exe, addr);
    system(buf);
  }
}

#endif
