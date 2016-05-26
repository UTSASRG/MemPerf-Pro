#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "real.hh"
#include "memsample.h"

class xthread {
	typedef void * threadFunction(void *);
	typedef struct thread {
		pid_t tid;
		threadFunction * startRoutine;
		void * startArg;
		void * result;
	} thread_t;

	public:
	static int thread_create(pthread_t * tid, const pthread_attr_t * attr, threadFunction * fn, void * arg) {
		thread_t * children = (thread_t *) malloc(sizeof(thread_t));
		children->startArg = arg;
		children->startRoutine = fn;
		int result = Real::pthread_create(tid, attr, xthread::startThread, (void *)children);
		if(result != 0) {
			fprintf(stderr, "ERROR: pthread_create failed with errno=%d: %s\n", errno, strerror(errno));
			abort();
		}
		return result;
	}

	static void * startThread(void * arg) {
		initSampling();

		void * result = NULL;
		thread_t * current = (thread_t *) arg;
		pid_t tid = syscall(__NR_gettid);
		current->tid = tid;

		result = current->startRoutine(current->startArg);
		return result;
	}
};
