#include <sys/syscall.h>

#include <stdio.h>

#include "memsample.h"
#include "real.hh"

__thread extern thread_data thrData;

extern "C" void printHashMap();
extern "C" pid_t gettid();

thread_local extern uint64_t numWaits;
thread_local extern uint64_t timeWaiting;
thread_local extern uint64_t thread_stack_start;

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
		thread_t * children = (thread_t *) RealX::malloc(sizeof(thread_t));
		children->thread = tid;
		children->startArg = arg;
		children->startRoutine = fn;

//		int result = RealX::pthread_create(tid, attr, xthreadx::startThread, (void *)children);
		int result = RealX::pthread_create(tid, attr, xthreadx::startThread_noFile, (void *)children);
		if(result) {
			perror("ERROR: pthread_create failed");
			abort();
		}

		return result;
	}

	static void * startThread(void * arg) {
		void * result = NULL;
		size_t stackSize;
		thread_t * current = (thread_t *) arg;

		pid_t pid = getpid();
		pid_t tid = gettid();
		current->tid = tid;

		char outputFile[MAX_FILENAME_LEN];

		snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmallocprof_%d_tid_%d.txt",
				program_invocation_name, pid, tid);

		// Presently set to overwrite file; change fopen flag to "a" for append.
		thrData.output = fopen(outputFile, "w");
		if(thrData.output == NULL) {
			fprintf(stderr, "error: unable to open output file for writing hash map\n");
			fprintf(stderr, "error: %s\n", strerror (errno));
			abort();
		}

		pthread_attr_t attrs;
		if(pthread_getattr_np(pthread_self(), &attrs) != 0) {
			fprintf(stderr, "error: unable to get thread attributes: %s\n", strerror(errno));
			abort();
		}
		if(pthread_attr_getstack(&attrs, (void **)&thrData.stackEnd, &stackSize) != 0) {
			fprintf(stderr, "error: unable to get stack values: %s\n", strerror(errno));
			abort();
		}
		char * firstHeapObj = (char *)RealX::malloc(sizeof(char));
		thrData.stackStart = thrData.stackEnd + stackSize;
		RealX::free(firstHeapObj);

		fprintf(thrData.output, ">>> thread %d stack start @ %p, stack end @ %p\n", tid,
				  thrData.stackStart, thrData.stackEnd);

		initSampling();
		result = current->startRoutine(current->startArg);
		doPerfRead();

		fprintf (thrData.output, ">>> numWaits = %zu\n", numWaits);
		fprintf (thrData.output, ">>> timeWaiting = %zu\n", timeWaiting);

		fclose(thrData.output);
		return result;
	}

	static void * startThread_noFile(void * arg) {
		void * result = NULL;
		size_t stackSize;
		thread_t * current = (thread_t *) arg;

		pid_t tid = gettid();
		current->tid = tid;

		pthread_attr_t attrs;
		if(pthread_getattr_np(pthread_self(), &attrs) != 0) {
			fprintf(stderr, "error: unable to get thread attributes: %s\n", strerror(errno));
			abort();
		}
		if(pthread_attr_getstack(&attrs, (void **)&thrData.stackEnd, &stackSize) != 0) {
			fprintf(stderr, "error: unable to get stack values: %s\n", strerror(errno));
			abort();
		}

		char * firstHeapObj = (char *)RealX::malloc(sizeof(char));
		thrData.stackStart = thrData.stackEnd + stackSize;
		RealX::free(firstHeapObj);

		initSampling();
		result = current->startRoutine(current->startArg);
		doPerfRead_noFile();

		return result;
	}
};
