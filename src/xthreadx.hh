#include <sys/syscall.h>

#include <stdio.h>

#include "libmallocprof.h"
#include "memsample.h"
#include "real.hh"

extern thread_local thread_data thrData;
extern thread_local unsigned long long total_cycles_start;
extern std::atomic<std::uint64_t> total_global_cycles;

extern "C" void setThreadContention();
extern "C" void printHashMap();
//extern "C" pid_t gettid();
void* myMalloc(size_t);
extern void initMyLocalMem();

thread_local extern uint64_t thread_stack_start;
thread_local extern uint64_t myThreadID;
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
		thread_t * children = (thread_t *) myMalloc(sizeof(thread_t));
		children->thread = tid;
		children->startArg = arg;
		children->startRoutine = fn;

		total_cycles_start = rdtscp();
		int result = RealX::pthread_create(tid, attr, xthreadx::startThread, (void *)children);
		if(result) {
			fprintf(stderr, "error: pthread_create failed: %s\n", strerror(errno));
		}
		unsigned long long total_cycles_end = rdtscp();
		total_global_cycles += total_cycles_end - total_cycles_start;

		return result;
	}

	static void * startThread(void * arg) {

		myThreadID = pthread_self();
  
        // set thread local storeage
        setThreadContention();

		initMyLocalMem();

		void * result = NULL;
		size_t stackSize;
		thread_t * current = (thread_t *) arg;

		pid_t tid = gettid();
		thrData.tid = tid;

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
		initPMU();
		#endif
		result = current->startRoutine(current->startArg);

		threadExit();

		return result;
	}

  static void threadExit() {
    #ifndef NO_PMU
    stopSampling();
    doPerfCounterRead();
    stopCounting();
    #endif

    // Replicate this thread's application friendliness data before it exits.
    updateGlobalFriendlinessData();

    if(thrData.output) {
      fclose(thrData.output);
    }
    globalizeTAD();
	}
};
