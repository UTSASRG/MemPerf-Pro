#ifndef __THREADSTRUCT_HH__
#define __THREADSTRUCT_HH__

#include <stddef.h>
#include <ucontext.h>
#include <pthread.h>
#include "xdefines.hh"

extern "C" {
		typedef struct thread {
				// Whether the entry is available so that allocThreadIndex can use this one
				bool available;

				// Identifications
				pid_t tid;
				pthread_t pthreadt;
				int index;

				// Only used in thread joining so that my parent can wait on it.
				pthread_spinlock_t spinlock;

				// Starting parameters
				threadFunction * startRoutine;
				void * startArg;

				// Printing buffer to avoid lock conflict. 
				// fprintf will use a shared buffer and can't be used in signal handler
				char outputBuf[LOG_SIZE];
		} thread_t;

		typedef __declspec(align(CACHE_LINE_SIZE)) struct {
				int tindex;
				unsigned long numCalls;
				unsigned long timeSum; 
				size_t sizeSum; 
		} madv_t;
};
#endif
