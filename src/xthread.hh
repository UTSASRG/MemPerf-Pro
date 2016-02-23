
// -*- C++ -*-

/*
Allocate and manage thread index.
Second, try to maintain a thread local variable to save some thread local information.
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
 * @file   xthread.h
 * @brief  Managing the thread creation, etc.
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */

#ifndef _XTHREAD_H_
#define _XTHREAD_H_

#include <new>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h> //mejbah
#include <assert.h> //mejbah
#include <fstream>
#include <iostream>
#include "xdefines.hh"

class xthread {
private:
	xthread() 
    { }
		
public:
  static xthread& getInstance() {
    static char buf[sizeof(xthread)];
    static xthread * theOneTrueObject = new (buf) xthread();
    return *theOneTrueObject;
  }

  /// @brief Initialize the system.
  void initialize()
  {
    _heapid = 0;

    // Shared the threads information. 
    memset(&_threads, 0, sizeof(_threads));

		// Set all entries to be available initially 
    for(int i = 0; i < xdefines::NUM_HEAPS; i++) {
			_HeapAvailable[i] = true;
    }

    // Allocate the threadindex for current thread.
    initInitialThread();
  }

	// The end of system. 
	void finalize(void) {
	}


    /// @ Allocation should be always successful.
  int allocHeapId(void) {
    int heapid;

    while(true) {
      if(_HeapAvailable[_heapid] == true) {
        heapid = _heapid;
        _HeapAvailable[_heapid] = false;
        _heapid = (_heapid+1)%xdefines::NUM_HEAPS;
        break;
      }
      _heapid = (_heapid+1)%xdefines::NUM_HEAPS;
    }

    return heapid;
  }

  // Set the heap to be available if the thread is exiting.
  void releaseHeap(int heapid) {
    _HeapAvailable[heapid] = true;
  }

  // Initialize the first threadd
  void initInitialThread(void) {
    int tindex;

    // Allocate a global thread index for current thread.
    tindex = allocThreadIndex();

		// We know that we will going to execute     
    current = getThreadInfoByIndex(tindex);

		assert(tindex == 0);

    // Get corresponding thread_t structure.
		current->index = tindex;
    current->self  = pthread_self();
    current->tid  = gettid();
    current->stackTop =(void*)(((intptr_t)current->self + xdefines::PageSize) & ~xdefines::PAGE_SIZE_MASK);
  }

  thread_t * getThreadInfoByIndex(int index) {
		assert(index < xdefines::MAX_THREADS);

    return &_threads[index];
  }

	unsigned long getMaxThreadIndex(void) {
		return _threadIndex;
  }
	
  // Allocate a thread index under the protection of global lock
  int allocThreadIndex(void) {
		int index = __atomic_fetch_add(&_threadIndex, 1, __ATOMIC_RELAXED);

		// Check whether we have created too many threads or there are too many alive threads now.
    if(index >= xdefines::MAX_THREADS || _alivethreads >= xdefines::MAX_ALIVE_THREADS) {
      fprintf(stderr, "Set xdefines::MAX_THREADS to larger. _alivethreads %ld totalthreads %ld maximum alive threads %d", _aliveThreads, _threadIndex, xdefines::MAX_ALIVE_THREADS);
      abort(); 
    } 

		    // Initialize 
    thread_t * thread = getThreadInfoByIndex(index);
    thread->index = index;

    // Now find one available heapid for this thread.
    thread->heapid = allocHeapId();
    return index; 
  }

  /// Create the wrapper 
  /// @ Intercepting the thread_creation operation.
  int thread_create(pthread_t * tid, const pthread_attr_t * attr, threadFunction * fn, void * arg) {
    void * ptr = NULL;
    int tindex;
    int result;

    // Allocate a global thread index for current thread.
    tindex = allocThreadIndex();
		//printf("pthread create  index : %d\n", tindex);
    thread_t * children = getThreadInfoByIndex(tindex);
    
    children->startRoutine = fn;
    children->startArg = arg;
	
    result =  WRAP(pthread_create)(tid, attr, startThread, (void *)children);

    return result;
  }      


  int thread_join(pthread_t thread, void **retval)  {
    int ret;

    ret = WRAP(pthread_join(thread, retval));

    if(ret == 0) {
      thread_t * thisThread;

      // Finding out the thread with this pthread_t 
      thisThread = getChildThreadStruct(thread);

      markThreadExit(thisThread);
    }

    return ret;
  }

	void markThreadExit(thread_t * thread) {
    --_aliveThreads;
		    
		// Release the heap id for this thread.
    releaseHeap(thread->heapid);
	}

  // @Global entry of all entry function.
  static void * startThread(void * arg) {
    void * result;

    current = (thread_t *)arg;

    current->self = pthread_self();
		current->tid = gettid();
    current->stackTop =(void*)(((intptr_t)current->self + xdefines::PageSize) & ~xdefines::PAGE_SIZE_MASK);

    // from the TLS storage.
    result = current->startRoutine(current->startArg);

		// Get the stop time.
		current->actualRuntime = elapsed2ms(stop(&current->startTime, NULL));
		//fprintf(stderr, "tid %d index %d latency %lx actualRuntime %ld\n", current->tid, current->index, current->latency, current->actualRuntime);

    return result;
  }

	void* getPrivateStackTop() {return current->stackTop;}
	
private:
	volatile unsigned long _aliveThreads;
  volatile unsigned long _threadIndex;
	int _heapid;	

	bool     _HeapAvailable[xdefines::NUM_HEAPS];
  // Total threads we can support is MAX_THREADS
  thread_t  _threads[xdefines::MAX_THREADS];

};
#endif

