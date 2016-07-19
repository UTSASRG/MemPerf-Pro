#include <sys/syscall.h>

#include "libtestperf.h"
#include "real.hh"

extern FILE * output;

class xthread {
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
		thread_t * children = (thread_t *) malloc(sizeof(thread_t));
		children->thread = tid;
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
		void * result = NULL;
		thread_t * current = (thread_t *) arg;
		pid_t pid = getpid();
		pid_t tid = gettid();
		current->tid = tid;

		char outputFile[MAX_FILENAME_LEN];
		snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmemperf_pid_%d_tid_%d.txt",
				program_invocation_name, pid, tid);

		// Presently set to overwrite file; change fopen flag to "a" for append.
		output = fopen(outputFile, "w");
		if(output == NULL) {
			fprintf(stderr, "error: unable to open output file for writing hash map\n");
			abort();
		}

		result = current->startRoutine(current->startArg);

		fclose(output);
		return result;
	}
};
