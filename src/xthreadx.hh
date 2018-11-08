#include <sys/syscall.h>

#include <stdio.h>

#include "libmallocprof.h"
#include "memsample.h"
#include "real.hh"

extern __thread thread_data thrData;

extern "C" void printHashMap();
extern "C" pid_t gettid();
void* myMalloc(size_t);
void initMyLocalMem();

thread_local extern uint64_t thread_stack_start;
thread_local extern uint64_t myThreadID;
thread_local extern perf_info perfInfo;
#ifdef USE_THREAD_LOCAL
thread_local extern uint64_t myLocalPosition;
#endif

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
		thread_t * children = (thread_t *) myMalloc(sizeof(thread_t));
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

		myThreadID = pthread_self();

		#ifdef USE_THREAD_LOCAL
			initMyLocalMem();
		#endif

		void * result = NULL;
		size_t stackSize;
		thread_t * current = (thread_t *) arg;

		pid_t tid = gettid();
		current->tid = tid;

		#ifdef THREAD_OUTPUT
		pid_t pid = getpid();
		char outputFile[MAX_FILENAME_LEN];

		snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmallocprof_%d_tid_%d.txt",
						program_invocation_name, pid, tid);

		// Presently set to overwrite file; change fopen flag to "a" for append.
		thrData.output = fopen(outputFile, "w");
		if(thrData.output == NULL) {
				fprintf(stderr, "error: unable to open output file for writing hash map: %s\n", strerror(errno));
				current->output = false;
		}
		#else
		thrData.output = NULL;
		#endif

		pthread_attr_t attrs;
		if(pthread_getattr_np(pthread_self(), &attrs) != 0) {
			fprintf(stderr, "error: unable to get thread attributes: %s\n", strerror(errno));
			abort();
		}
		if(pthread_attr_getstack(&attrs, (void **)&thrData.stackEnd, &stackSize) != 0) {
			fprintf(stderr, "error: unable to get stack values: %s\n", strerror(errno));
			abort();
		}
		thrData.stackStart = thrData.stackEnd + stackSize;

		if(thrData.output) {
			fprintf(thrData.output, ">>> thread %d stack start @ %p, stack end @ %p\n", tid,
				thrData.stackStart, thrData.stackEnd);
		}

		#ifndef NO_PMU
		initSampling();
		#endif
		result = current->startRoutine(current->startArg);
		#ifndef NO_PMU
		doPerfRead();
		#endif

		#ifdef USE_THREAD_LOCAL
		fprintf (stderr, "Thread %lu myLocalPosition= %zu\n", myThreadID, myLocalPosition);
		#endif

		if(thrData.output) {
			fclose(thrData.output);
		}

		globalizeTAD();

		close(perfInfo.perf_fd_fault);
		close(perfInfo.perf_fd_tlb_reads);
		close(perfInfo.perf_fd_tlb_writes);
		close(perfInfo.perf_fd_cache_miss);
		close(perfInfo.perf_fd_instr);

		return result;
	}
};
