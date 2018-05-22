/**
 * @file libmemperf.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 * @author Sam Silvestro <sam.silvestro@utsa.edu>
 */

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"

#include <dlfcn.h>
#include <malloc.h>
#include <alloca.h>

#include <map>
#include <cstdlib>

#include "memsample.h"
#include "real.hh"
#include "xthread.hh"
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "spinlock.hh"

typedef HashMap<uint64_t, ObjectTuple*, spinlock> HashMapX;

__thread thread_data thrData;

// This map will go from the callsite ID key to a structure containing:
//	(1) the first callsite
//	(2) the second callsite
//	(3) the total number of allocations originating from this callsite pair
//	(4) the total number of frees originating from this callsite pair
//	(5) the total size of these freed objects (i.e., when using glibc malloc,
//		total usable size (no header))
//	(6) the total size of these allocations (i.e., when using glibc malloc,
//		total usable size (no header))
//	(7) the requested/used size of these allocations (i.e., the sum of sz taken
//		from each malloc(sz) call)
//	(8) the total number of accesses on objects spawned from this callsite id
HashMapX mapCallsiteStats;

extern char data_start;
extern char _etext;
extern char * program_invocation_name;
extern void * __libc_stack_end;
extern char __executable_start;

bool const debug = false;
bool mallocInitialized = false;
bool shadowMemZeroedOut = false;
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
char * tmpbuf;
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
	void * aligned_alloc(size_t, size_t) __attribute__ ((weak,
				alias("yyaligned_alloc")));
	void * memalign(size_t, size_t) __attribute__ ((weak, alias("yymemalign")));
	void * pvalloc(size_t) __attribute__ ((weak, alias("yypvalloc")));
	void * alloca(size_t) __attribute__ ((weak, alias("yyalloca")));
	int posix_memalign(void **, size_t, size_t) __attribute__ ((weak,
				alias("yyposix_memalign")));
}

__attribute__((constructor)) void initializer() {
	// Ensure we are operating on a system using 64-bit pointers.
	// This is necessary, as later we'll be taking the low 8-byte word
	// of callsites. This could obviously be expanded to support 32-bit systems
	// as well, in the future.
	size_t ptrSize = sizeof(void *);
	if(ptrSize != EIGHT_BYTES) {
		fprintf(stderr, "error: unsupported pointer size: %zu\n", ptrSize);
		abort();
	}

	// Calculate the minimum possible callsite ID by taking the low four bytes
	// of the start of the program text and repeating them twice, back-to-back,
	// resulting in eight bytes which resemble: 0x<lowWord><lowWord>
	uint64_t btext = (uint64_t)&__executable_start & 0xFFFFFFFF;
	min_pos_callsite_id = (btext << 32) | btext;

	// Allocate memory for our pre-init temporary allocator
	if((tmpbuf = (char *)mmap(NULL, TEMP_BUF_SIZE, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
        perror("error: unable to allocate temporary memory");
		abort();
    }

	Real::initializer();
	mallocInitialized = true;
	void * program_break = sbrk(0);
	thrData.stackStart = (char *)__builtin_frame_address(0);
	thrData.stackEnd = (char *)__libc_stack_end;
	thrData.watchStartByte = (char *)program_break;
	thrData.watchEndByte = thrData.watchStartByte + SHADOW_MEM_SIZE - 1;

	// Initialize hashmap
	mapCallsiteStats.initialize(HashFuncs::hashCallsiteId,
			HashFuncs::compareCallsiteId, 4096);

	// Allocate shadow memory
	if((thrData.shadow_mem = (char *)mmap(NULL, SHADOW_MEM_SIZE,
					PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
					-1, 0)) == MAP_FAILED) {
		perror("error: unable to allocate shadow memory");
		abort();
	}

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

	fprintf(thrData.output, ">>> shadow memory allocated for main thread @ "
			"%p ~ %p (size=%ld bytes)\n", thrData.shadow_mem,
			thrData.shadow_mem + SHADOW_MEM_SIZE, SHADOW_MEM_SIZE);
	fprintf(thrData.output, ">>> stack start @ %p, stack end @ %p\n",
			thrData.stackStart, thrData.stackEnd);
	fprintf(thrData.output, ">>> watch start @ %p, watch end @ %p\n",
			thrData.watchStartByte, thrData.watchEndByte);
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

	long access_byte_offset =
		(char *)thrData.maxObjAddr - thrData.watchStartByte;
	char * shadow_mem_end = thrData.shadow_mem + SHADOW_MEM_SIZE;
	char * maxShadowObjAddr = thrData.shadow_mem + access_byte_offset;
	fprintf(thrData.output, ">>> heap memory used = %ld bytes\n",
			access_byte_offset);
	if(maxShadowObjAddr >= shadow_mem_end) {
		fprintf(thrData.output, ">>> WARNING: shadow memory was exceeded! "
				"maxObjAddr = %p (shadow: %p)\n\n", thrData.maxObjAddr,
				maxShadowObjAddr);
	}
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
				if(mallocInitialized)
					munmap(tmpbuf, TEMP_BUF_SIZE);
				else
					tmppos = 0;
			}
		} else {
			if(!shadowMemZeroedOut &&
					((ptr >= (void *)thrData.watchStartByte) &&
					(ptr <= (void *)thrData.watchEndByte))) {
				//freeShadowMem(ptr);
			}

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

		if(ptr != NULL) {
			if(!shadowMemZeroedOut &&
					((ptr >= (void *)thrData.watchStartByte) &&
					(ptr <= (void *)thrData.watchEndByte))) {
				//freeShadowMem(ptr);
			}
		}

		void * reptr = Real::realloc(ptr, sz);

		if(!shadowMemZeroedOut) {
			//initShadowMem(reptr, sz);
		}

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
