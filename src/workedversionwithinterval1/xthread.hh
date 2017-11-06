#include <sys/syscall.h>

#include "memsample.h"
#include "real.hh"

__thread extern char * shadow_mem;
__thread extern char * stackStart;
__thread extern char * stackEnd;
__thread extern char * watchStartByte;
__thread extern char * watchEndByte;
__thread extern void * maxObjAddr;
__thread extern FILE * output;

extern "C" void printHashMap();
extern "C" pid_t gettid();

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
		size_t stackSize;
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

		if((shadow_mem = (char *)mmap(NULL, SHADOW_MEM_SIZE, PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
			fprintf(stderr, "error: unable to allocate shadow memory: %s\n", strerror(errno));
			abort();
		}

		fprintf(output, ">>> shadow memory allocated for child thread %d @ %p ~ %p "
				"(size=%ld bytes)\n", tid, shadow_mem, shadow_mem + SHADOW_MEM_SIZE,
				SHADOW_MEM_SIZE);

		pthread_attr_t attrs;
		if(pthread_getattr_np(pthread_self(), &attrs) != 0) {
			fprintf(stderr, "error: unable to get thread attributes: %s\n", strerror(errno));
			abort();
		}
		if(pthread_attr_getstack(&attrs, (void **)&stackStart, &stackSize) != 0) {
			fprintf(stderr, "error: unable to get stack values: %s\n", strerror(errno));
			abort();
		}
		char * firstHeapObj = (char *)malloc(sizeof(char));
		watchStartByte = firstHeapObj - MALLOC_HEADER_SIZE;
		watchEndByte = watchStartByte + SHADOW_MEM_SIZE;
		stackEnd = stackStart + stackSize;
		free(firstHeapObj);

		fprintf(output, ">>> thread %d stack start @ %p, stack end @ %p\n", tid,
				stackStart, stackEnd);
		fprintf(output, ">>> watch start @ %p, watch end @ %p\n",
				watchStartByte, watchEndByte);

		initSampling();
		result = current->startRoutine(current->startArg);
		stopSampling();

		long access_byte_offset = (char *)maxObjAddr - (char *)watchStartByte;
		char * maxShadowObjAddr = (char *)shadow_mem + access_byte_offset;
		fprintf(output, ">>> maxObjAddr = %p/%p\n", maxObjAddr, maxShadowObjAddr);

		fclose(output);
		return result;
	}
};
