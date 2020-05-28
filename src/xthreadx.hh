#include <sys/syscall.h>

#include <stdio.h>

#include "libmallocprof.h"
#include "memsample.h"
#include "real.hh"
#include "memwaste.h"
#include "mymalloc.h"
#include "threadlocalstatus.h"

thread_local extern perf_info perfInfo;


class xthreadx {
	typedef void * threadFunction(void *);
	typedef struct thread {
		pthread_t * thread;
		pid_t tid;
		threadFunction * startRoutine;
		void * startArg;
		void * result;
	} thread_t;

	public:
	static int thread_create(pthread_t * tid, const pthread_attr_t * attr, threadFunction * fn, void * arg) {
		thread_t * children = (thread_t *) MyMalloc::malloc(sizeof(thread_t));
		children->thread = tid;
		children->startArg = arg;
		children->startRoutine = fn;

		int result = RealX::pthread_create(tid, attr, xthreadx::startThread, (void *)children);
		if(result) {
			fprintf(stderr, "error: pthread_create failed: %s\n", strerror(errno));
		}

		return result;
	}

	static void * startThread(void * arg) {

        ThreadLocalStatus::getARunningThreadIndex();

		void * result = NULL;
		size_t stackSize;
		thread_t * current = (thread_t *) arg;

		pthread_attr_t attrs;
		if(pthread_getattr_np(pthread_self(), &attrs) != 0) {
			fprintf(stderr, "error: unable to get thread attributes: %s\n", strerror(errno));
			abort();
		}

		#ifndef NO_PMU
		initPMU();
        #endif
		result = current->startRoutine(current->startArg);

		threadExit();

		return result;
	}


  static void threadExit()
    #ifndef NO_PMU
    stopSampling();
    stopCounting();
    #endif

    updateGlobalFriendlinessData();

    GlobalStatus::globalize();
	}
};
