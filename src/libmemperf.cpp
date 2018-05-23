/**
 * @file libmemperf.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 * @author Sam Silvestro <sam.silvestro@utsa.edu>
 */

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <dlfcn.h>
#include <malloc.h>
#include <alloca.h>

#include <sys/mman.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <unistd.h>

#include <map>
#include <cstdlib>

#include "memsample.h"
#include "real.hh"
#include "xthread.hh"
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "spinlock.hh"
#include "selfmap.hh"
#include <libsyscall_intercept_hook_point.h>

#define CALLSITE_MAXIMUM_LENGTH 10

typedef HashMap<uint64_t, ObjectTuple*, spinlock> HashMapX;
spinlock mmap_lock;
int numMmaps;
size_t mmapSize;
extern int (*intercept_hook_point)(long syscall_number,
      long arg0, long arg1,
      long arg2, long arg3,
      long arg4, long arg5,
      long *result);

__thread thread_data thrData;

HashMapX mapCallsiteStats;

extern char data_start;
extern char _etext;
extern char * program_invocation_name;
extern void * __libc_stack_end;
extern char __executable_start;

bool const debug = false;
bool mallocInitialized = false;
uint64_t min_pos_callsite_id;

// Rich
int numNewAlloc = 0;
int numReuseAlloc = 0;
int numFrees = 0;
unsigned long cyclesNewAlloc = 0;
unsigned long cyclesReuseAlloc = 0;
unsigned long cyclesFree = 0;
bool bumpPointer = false;
bool bibop = false;

void getInfo ();
ObjectTuple* newObjectTuple (int numAllocs, size_t objectSize);

// Variables used by our pre-init private allocator
char tmpbuf[TEMP_BUF_SIZE];
unsigned int numTempAllocs = 0;
unsigned int tmppos = 0;

struct stack_frame {
	struct stack_frame * prev;	// pointing to previous stack_frame
	void * caller_address;		// the address of caller
};

typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main;

extern "C" {
	// Function prototypes
	addrinfo addr2line(void * addr);
	size_t getTotalAllocSize(size_t sz);
	void exitHandler();
	inline void getCallsites(void **callsites);

	// Function aliases
	void free(void *) __attribute__ ((weak, alias("yyfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("yycalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("yymalloc")));
	void * realloc(void *, size_t) __attribute__ ((weak, alias("yyrealloc")));
	void * valloc(size_t) __attribute__ ((weak, alias("yyvalloc")));
	void * mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) __attribute__ ((weak, alias("yymmap")));
	void * aligned_alloc(size_t, size_t) __attribute__ ((weak,
				alias("yyaligned_alloc")));
	void * memalign(size_t, size_t) __attribute__ ((weak, alias("yymemalign")));
	void * pvalloc(size_t) __attribute__ ((weak, alias("yypvalloc")));
	void * alloca(size_t) __attribute__ ((weak, alias("yyalloca")));
	int posix_memalign(void **, size_t, size_t) __attribute__ ((weak,
				alias("yyposix_memalign")));
}

int xxintercept_hook_point(long syscall_number,
        long arg0, long arg1,
        long arg2, long arg3,
        long arg4, long arg5,
        long *result);

__attribute__((constructor)) void initializer() {
	if(mallocInitialized) { return; }
	intercept_hook_point = xxintercept_hook_point;

	// Ensure we are operating on a system using 64-bit pointers.
	// This is necessary, as later we'll be taking the low 8-byte word
	// of callsites. This could obviously be expanded to support 32-bit systems
	// as well, in the future.
	size_t ptrSize = sizeof(void *);
	if(ptrSize != EIGHT_BYTES) {
		fprintf(stderr, "error: unsupported pointer size: %zu\n", ptrSize);
		abort();
	}

	mmap_lock.init();

	// Calculate the minimum possible callsite ID by taking the low four bytes
	// of the start of the program text and repeating them twice, back-to-back,
	// resulting in eight bytes which resemble: 0x<lowWord><lowWord>
	uint64_t btext = (uint64_t)&__executable_start & 0xFFFFFFFF;
	min_pos_callsite_id = (btext << 32) | btext;

	/*
	// Allocate memory for our pre-init temporary allocator
	if((tmpbuf = (char *)mmap(NULL, TEMP_BUF_SIZE, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
        perror("error: unable to allocate temporary memory");
		abort();
  }
	*/

	Real::initializer();
	selfmap::getInstance().getTextRegions();
	mallocInitialized = true;
	void * program_break = sbrk(0);
	thrData.stackStart = (char *)__builtin_frame_address(0);
	thrData.stackEnd = (char *)__libc_stack_end;

	// Initialize hashmap
	mapCallsiteStats.initialize(HashFuncs::hashCallsiteId,
			HashFuncs::compareCallsiteId, 4096);

	// Generate the name of our output file, then open it for writing.
	char outputFile[MAX_FILENAME_LEN];
	pid_t pid = getpid();
	snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmemperf_pid_%d_tid_%d.txt",
		program_invocation_name, pid, pid);
	// Will overwrite current file; change the fopen flag to "a" for append.
	thrData.output = fopen(outputFile, "w");
	if(thrData.output == NULL) {
		perror("error: unable to open output file to write");
		return;
	}

	fprintf(thrData.output, ">>> stack start @ %p, stack end @ %p\n",
			thrData.stackStart, thrData.stackEnd);
	fprintf(thrData.output, ">>> program break @ %p\n", program_break);
	fflush(thrData.output);

	getInfo ();
}

__attribute__((destructor)) void finalizer() {
	fclose(thrData.output);
}

void writeHashMap (void) {

	HashMapX::iterator iterator;
		for (iterator = mapCallsiteStats.begin();
			  iterator != mapCallsiteStats.end();
			  iterator++) {
			
			ObjectTuple* tuple = iterator.getData();	

		}
}

void writeAllocData () {

	fprintf (thrData.output, ">>> Number of mmap calls: %d\n", numMmaps);
	fprintf (thrData.output, ">>> Total size of mmap calls: %zu\n", mmapSize);

	fprintf (thrData.output, ">>> Number of new allocations: %d\n", numNewAlloc);
	fprintf (thrData.output, ">>> Number of re allocations:  %d\n", numReuseAlloc);
	fprintf (thrData.output, ">>> Number of frees:           %d\n", numFrees);
	fprintf (thrData.output, ">>> Avg cycles of new alloc:    %lu clock cycles\n",
				(cyclesNewAlloc / (long)numNewAlloc));
	fprintf (thrData.output, ">>> Avg cycles of reuse alloc:  %lu clock cycles\n",
				(cyclesReuseAlloc / (long)numReuseAlloc));
	fprintf (thrData.output, ">>> Avg cycles for free:        %lu clock cycles \n",
				(cyclesFree / (long)numFrees));
}

void exitHandler() {
	doPerfRead();

	fflush(thrData.output);
	writeAllocData ();
//	writeHashmap ();
}

// MemPerf's main function
int libmemperf_main(int argc, char ** argv, char ** envp) {
	// Register our cleanup routine as an on-exit handler.
	atexit(exitHandler);

	initSampling();

	return real_main(argc, argv, envp);
}

extern "C" int __libc_start_main(main_fn_t, int, char **, void (*)(),
		void (*)(), void (*)(), void *) __attribute__((weak,
			alias("libmemperf_libc_start_main")));

extern "C" int libmemperf_libc_start_main(main_fn_t main_fn, int argc,
		char ** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(),
		void * stack_end) {
	intercept_hook_point = xxintercept_hook_point;
	auto real_libc_start_main =
		(decltype(__libc_start_main) *)dlsym(RTLD_NEXT, "__libc_start_main");
	real_main = main_fn;
	return real_libc_start_main(libmemperf_main, argc, argv, init, fini,
			rtld_fini, stack_end);
}

// Memory management functions
extern "C" {
	void * yymalloc(size_t sz) {
		// Small allocation routine designed to service malloc requests made by
		// the dlsym() function, as well as code running prior to dlsym(). Due
		// to our linkage alias which redirects malloc calls to yymalloc
		// located in this file; when dlsym calls malloc, yymalloc is called
		// instead. Without this routine, yymalloc would simply rely on
		// Real::malloc to fulfill the request, however, Real::malloc would not
		// yet be assigned until the dlsym call returns. This results in a
		// segmentation fault. To remedy the problem, we detect whether the Real
		// has finished initializing; if it has not, we fulfill malloc requests
		// using a memory mapped region. Once dlsym finishes, all future malloc
		// requests will be fulfilled by Real::malloc, which itself is a
		// reference to the real glibc malloc routine.
		if(!mallocInitialized) {
			if((tmppos + sz) < TEMP_BUF_SIZE) {
				void * retptr = (void *)(tmpbuf + tmppos);
				tmppos += sz;
				numTempAllocs++;
				return retptr;
			} else {
				fprintf(stderr, "error: global allocator out of memory\n");
				fprintf(stderr, "\t requested size = %zu, total size = %d, "
					"total allocs = %u\n", sz, TEMP_BUF_SIZE, numTempAllocs);
				abort();
			}
		}

		uint64_t before = rdtscp ();

		void* objAlloc = Real::malloc(sz);

		uint64_t after = rdtscp ();

		uint64_t cyclesForMalloc = after - before;

		uint64_t address = reinterpret_cast <uint64_t> (objAlloc);

		ObjectTuple* found_value;

		if(mapCallsiteStats.find(address, &found_value)) {
			found_value->numAllocs++;
			found_value->size = sz;
			numReuseAlloc ++;
			cyclesReuseAlloc += cyclesForMalloc;
		}
		else {
			ObjectTuple* tuple = newObjectTuple (1, sz);
			mapCallsiteStats.insertIfAbsent(address, tuple);
			numNewAlloc ++;
			cyclesNewAlloc += cyclesForMalloc;
		}

		return objAlloc;
	}

	void * yycalloc(size_t nelem, size_t elsize) {
		void * ptr = NULL;
		if((ptr = malloc(nelem * elsize)))
			memset(ptr, 0, nelem * elsize);
		return ptr;
	}

	void yyfree(void * ptr) {
		if(ptr == NULL)
			return;

		// Determine whether the specified object came from our global buffer;
		// only call Real::free() if the object did not come from here.
		if((ptr >= (void *)tmpbuf) &&
				(ptr <= (void *)(tmpbuf + TEMP_BUF_SIZE))) {
			numTempAllocs--;
			if(numTempAllocs == 0) {
					tmppos = 0;
			}
		} else {
			uint64_t before = rdtscp ();

			Real::free(ptr);

			uint64_t after = rdtscp ();

			numFrees ++;
			cyclesFree += (after - before);
		}
	}

	inline void logUnsupportedOp() {
		fprintf(thrData.output,
				"ERROR: call to unsupported memory function: %s\n",
				__FUNCTION__);
	}
	void * yyalloca(size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * yyvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	int yyposix_memalign(void **memptr, size_t alignment, size_t size) {
		logUnsupportedOp();
		return -1;
	}
	void * yyaligned_alloc(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * yymemalign(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * yypvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
	}

	void * yyrealloc(void * ptr, size_t sz) {
		if(!mallocInitialized) {
			if(ptr == NULL)
				return yymalloc(sz);
			yyfree(ptr);
			return yymalloc(sz);
		}

		void * reptr = Real::realloc(ptr, sz);

		return reptr;
	}

	inline void getCallsites(void **callsites) {
		int i = 0;
		void * btext = &__executable_start;
		void * etext = &data_start;

		// Fetch the frame address of the topmost stack frame
		struct stack_frame * current_frame =
			(struct stack_frame *)(__builtin_frame_address(0));

		// Initialize the prev_frame pointer to equal the current_frame. This
		// simply ensures that the while loop below will be entered and
		// executed and least once
		struct stack_frame * prev_frame = current_frame;

		// Initialize the array elements
		callsites[0] = (void *)NULL;
		callsites[1] = (void *)NULL;

		// Loop condition tests the validity of the frame address given for the
		// previous frame by ensuring it actually points to a location located
		// on the stack
		while((i < 2) && ((void *)prev_frame <= (void *)thrData.stackStart) &&
				(prev_frame >= current_frame)) {
			// Inspect the return address belonging to the previous stack frame;
			// if it's located in the program text, record it as the next
			// callsite
			void * caller_addr = prev_frame->caller_address;
			if((caller_addr >= btext) && (caller_addr <= etext)) {
				callsites[i++] = caller_addr;
			}
			// Walk the prev_frame pointer backward in preparation for the
			// next iteration of the loop
			prev_frame = prev_frame->prev;
		}
	}

	/*
	 * This function returns the total usable size of an object allocated
	 * using glibc malloc (and therefore it does not include the space occupied
	 * by its 8 byte header).
 	 */
	size_t getTotalAllocSize(size_t sz) {
		size_t totalSize, usableSize;

		// Smallest possible total object size (including header) is 32 bytes,
		// thus making the total usable object size equal to 32-8=24 bytes.
		if(sz <= 24) {
			return 24;
		}

		// Calculate a total object size that is double-word aligned.
		totalSize = sz + 8;
		if(totalSize % 16 != 0) {
			totalSize = 16 * (((sz + 8) / 16) + 1);
			usableSize = totalSize - 8;
		} else {
			usableSize = sz;
		}

		return usableSize;
	}

	addrinfo addr2line(void * addr) {
		static bool initialized = false;
		static int fd[2][2];
		char strCallsite[20];
		char strInfo[512];
		addrinfo info;

		if(!initialized) {
			if((pipe(fd[0]) == -1) || (pipe(fd[1]) == -1)) {
				perror("error: unable to create pipe for addr2line\n");
				fprintf(thrData.output,
						"error: unable to create pipe for addr2line\n");
				strcpy(info.exename, "error");
				info.lineNum = 0;
				return info;
			}

			pid_t parent;
			switch(parent = fork()) {
				case -1:
					perror("error: unable to fork addr2line process\n");
					fprintf(thrData.output,
							"error: unable to fork addr2line process\n");
					strcpy(info.exename, "error");
					info.lineNum = 0;
					return info;
				case 0:		// child
					dup2(fd[1][0], STDIN_FILENO);
					dup2(fd[0][1], STDOUT_FILENO);
					// Close unneeded pipe ends for the child
					close(fd[0][0]);
					close(fd[1][1]);
					execlp("addr2line", "addr2line", "-s", "-e",
							program_invocation_name, "--", (char *)NULL);
					exit(EXIT_FAILURE);	// if we're still here then exec failed
					break;
				default:	// parent
					// Close unneeded pipe ends for the parent
					close(fd[0][1]);
					close(fd[1][0]);
					initialized = true;
			}
		}

		sprintf(strCallsite, "%p\n", addr);
		int szToWrite = strlen(strCallsite);
		if(write(fd[1][1], strCallsite, szToWrite) < szToWrite) {
			perror("error: incomplete write to pipe facing addr2line\n");
			fprintf(thrData.output,
					"error: incomplete write to pipe facing addr2line\n");
		}

		if(read(fd[0][0], strInfo, 512) == -1) {
			perror("error: unable to read from pipe facing addr2line\n");
			fprintf(thrData.output,
					"error: unable to read from pipe facing addr2line\n");
			strcpy(info.exename, "error");
			info.lineNum = 0;
			return info;
		}

		// Tokenize the return string, breaking apart by ':'.
		// Take the second token, which will be the line number.
		// Only copies the first 14 characters of the file name in order to
		// prevent misalignment in the program output.
		char * token = strtok(strInfo, ":");
		strncpy(info.exename, token, 14);
		info.exename[14] = '\0';	// null terminate the exename field
		token = strtok(NULL, ":");
		info.lineNum = atoi(token);

		return info;
	}

	// Intercept thread creation
	int pthread_create(pthread_t * tid, const pthread_attr_t * attr,
			void *(*start_routine)(void *), void * arg) {
		return xthread::thread_create(tid, attr, start_routine, arg);
	}
}

// Richard
// Create tuple for hashmap
ObjectTuple* newObjectTuple (int numAllocs, size_t objectSize) {

	ObjectTuple* objectTuple = (ObjectTuple*) Real::malloc (sizeof (ObjectTuple));	
	objectTuple->numAllocs = numAllocs;
	objectTuple->size = objectSize;
	return objectTuple;
}

// Try to figure out which allocator is being used
void getInfo () {

	void* add1 = Real::malloc (8);
	void* add2 = Real::malloc (2048);

	fprintf (thrData.output, ">>> add1: %p\n>>> add2: %p\n", add1, add2);

	long address1 = reinterpret_cast<long> (add1);
	long address2 = reinterpret_cast<long> (add2);
	long spaceBetween = std::abs((address2 - address1));

	fprintf (thrData.output, ">>> Space between objects: %ld bytes\n", spaceBetween);

	if (spaceBetween >= 4096) {

		bibop = true;
		fprintf (thrData.output, ">>> Allocator: bibop\n");
	}
	else {

		bumpPointer = true;
		fprintf (thrData.output, ">>> Allocator: bump pointer\n");
	}
}

inline bool isAllocatorInCallStack() {
        // Fetch the frame address of the topmost stack frame
        struct stack_frame * current_frame =
            (struct stack_frame *)(__builtin_frame_address(0));

        // Initialize the prev_frame pointer to equal the current_frame. This
        // simply ensures that the while loop below will be entered and
        // executed and least once
        struct stack_frame * prev_frame = current_frame;

        void * stackEnd = thrData.stackEnd;

				int allocatorLevel = -1;

				void * lastSeenAddress = NULL;

        int cur_depth = 0;
        while(((void *)prev_frame <= stackEnd) &&
                (prev_frame >= current_frame) && (cur_depth < CALLSITE_MAXIMUM_LENGTH)) {
						void * caller_address = prev_frame->caller_address;
						lastSeenAddress = caller_address;

						if(selfmap::getInstance().isAllocator(caller_address)) {
								printf("current caller is allocator: caller = %p, frame = %p\n", caller_address, prev_frame);
								//return true;
								//hasSeenAllocator = true;
								allocatorLevel = cur_depth;
						} else {
								printf("current caller is NOT the allocator: caller = %p, frame = %p\n", caller_address, prev_frame);
						}

            //in some case, "prev" address is the same as current address
            //or there is recursion 
            if(prev_frame == prev_frame->prev) {
								cur_depth++;
                break;
            }

            // Walk the prev_frame pointer backward in preparation for the
            // next iteration of the loop
            prev_frame = prev_frame->prev;

            cur_depth++;
				}

				printf("allocatorLevel = %d, cur_depth = %d\n", allocatorLevel, cur_depth);
				if((allocatorLevel > -1) && (allocatorLevel < cur_depth - 1)) {
				//if(hasSeenAllocator && !selfmap::getInstance().isAllocator(lastSeenAddress)) {
						return true;
				}	

				return false;
}

extern "C" void * yymmap(void *addr, size_t length, int prot, int flags,
				int fd, off_t offset) {
		initializer();
	
		if(isAllocatorInCallStack()) {
				mmap_lock.lock();
				numMmaps++;
				mmapSize += length;
				void * retval = Real::mmap(addr, length, prot, flags, fd, offset);
				mmap_lock.unlock();
				return retval;
		} else {
				return Real::mmap(addr, length, prot, flags, fd, offset);
		}
}

int xxintercept_hook_point(long syscall_number,
				long arg0, long arg1,
				long arg2, long arg3,
				long arg4, long arg5,
				long *result) {
		if(syscall_number == SYS_mmap) {
				if(isAllocatorInCallStack()) {
						mmap_lock.lock();
						numMmaps++;
						mmapSize += arg1;
						mmap_lock.unlock();
				}
		}
		return 1;
}
