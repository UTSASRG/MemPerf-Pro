#ifndef _XTHREAD_H_
#define _XTHREAD_H_

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "real.hh"
#include "memsample.h"

class xthread {
private:
	xthread() 
		{ }
		
public:
	typedef void * threadFunction (void *);
	static xthread& getInstance() {
		static char buf[sizeof(xthread)];
		static xthread * theOneTrueObject = new (buf) xthread();
		return *theOneTrueObject;
	}

	/// @brief Initialize the system.
	void initialize() { }

	// The end of system. 
	void finalize(void) { }

	/// Create the wrapper 
	/// @ Intercepting the thread_creation operation.
	int thread_create(pthread_t * tid, const pthread_attr_t * attr,
										threadFunction * fn, void * arg) {
		// TODO: Need to initialize sampling for this thread here.  -- Sam
		return Real::pthread_create(tid, attr, fn, arg);
	}    
};
#endif
