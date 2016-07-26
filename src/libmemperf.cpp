/**
 * @file libmemperf.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 * @author Sam Silvestro <sam.silvestro@utsa.edu>
 */

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"

#include <dlfcn.h>
#include <malloc.h>
#include <alloca.h>

#include "memsample.h"
#include "real.hh"
#include "xthread.hh"
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "spinlock.hh"

typedef HashMap<uint64_t, Tuple *, spinlock> HashMapX;

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
extern int numSamples;
extern int numSignals;

bool const debug = false;
bool mallocInitialized = false;
bool shadowMemZeroedOut = false;
uint64_t min_pos_callsite_id;

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
	bool isWordMallocHeader(long *word);
	size_t getTotalAllocSize(size_t sz);
	void countUnfreedObjAccesses();
	void exitHandler();
	void freeShadowMem(const void * ptr);
	void initShadowMem(const void * objAlloc, size_t sz);
	void writeHashMap();
	inline void getCallsites(void **callsites);

	// Function aliases
	void free(void *) __attribute__ ((weak, alias("xxfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("xxcalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("xxmalloc")));
	void * realloc(void *, size_t) __attribute__ ((weak, alias("xxrealloc")));
	void * valloc(size_t) __attribute__ ((weak, alias("xxvalloc")));
	void * aligned_alloc(size_t, size_t) __attribute__ ((weak,
				alias("xxaligned_alloc")));
	void * memalign(size_t, size_t) __attribute__ ((weak, alias("xxmemalign")));
	void * pvalloc(size_t) __attribute__ ((weak, alias("xxpvalloc")));
	void * alloca(size_t) __attribute__ ((weak, alias("xxalloca")));
	int posix_memalign(void **, size_t, size_t) __attribute__ ((weak,
				alias("xxposix_memalign")));
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
}

__attribute__((destructor)) void finalizer() {
	fclose(thrData.output);
}

void exitHandler() {
	stopSampling();

	long access_byte_offset =
		(char *)thrData.maxObjAddr - thrData.watchStartByte;
	char * shadow_mem_end = thrData.shadow_mem + SHADOW_MEM_SIZE;
	char * maxShadowObjAddr = thrData.shadow_mem + access_byte_offset;
	fprintf(thrData.output, ">>> numSamples = %d, numSignals = %d\n",
			numSamples, numSignals);
	fprintf(thrData.output, ">>> heap memory used = %ld bytes\n",
			access_byte_offset);
	if(maxShadowObjAddr >= shadow_mem_end) {
		fprintf(thrData.output, ">>> WARNING: shadow memory was exceeded! "
				"maxObjAddr = %p (shadow: %p)\n\n", thrData.maxObjAddr,
				maxShadowObjAddr);
	}
	fflush(thrData.output);
	countUnfreedObjAccesses();
	writeHashMap();
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
	void * xxmalloc(size_t sz) {
		// Small allocation routine designed to service malloc requests made by
		// the dlsym() function, as well as code running prior to dlsym(). Due
		// to our linkage alias which redirects malloc calls to xxmalloc
		// located in this file; when dlsym calls malloc, xxmalloc is called
		// instead. Without this routine, xxmalloc would simply rely on
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

		void * objAlloc = Real::malloc(sz);

		if(!shadowMemZeroedOut) {
			initShadowMem((const void *)objAlloc, sz);
		}

		return objAlloc;
	}

	Tuple * newTuple(void * callsite1, void * callsite2, int numAllocs,
			int numFrees, long szFreed, long szTotal, long szUsed,
			long numAccesses) {
		Tuple * new_tuple = (Tuple *)Real::malloc(sizeof(Tuple));
		new_tuple->callsite1 = callsite1;
		new_tuple->callsite2 = callsite2;
		new_tuple->numAllocs = numAllocs;
		new_tuple->numFrees = numFrees;
		new_tuple->szFreed = szFreed;
		new_tuple->szTotal = szTotal;
		new_tuple->szUsed = szUsed;
		new_tuple->numAccesses = numAccesses;
		return new_tuple;
	}

	/*
	* This routine is responsible for computing a callsite ID based on the two
	* callsite parameters, then writing this ID to the shadow memory which
	* corresponds to the given object's malloc header. Additionally, this
	* allocation is recorded in the global stats hash map.
	* Callers include xxmalloc and xxrealloc.
	*/
	void initShadowMem(const void * objAlloc, size_t sz) {
		void * callsites[2];
		getCallsites(callsites);
		void * callsite1 = callsites[0];
		void * callsite2 = callsites[1];

		size_t totalObjSz = getTotalAllocSize(sz);
		uint64_t lowWord1 = (uint64_t) callsite1 & 0xFFFFFFFF;
		uint64_t lowWord2 = (uint64_t) callsite2 & 0xFFFFFFFF;
		uint64_t callsite_id = (lowWord1 << 32) | lowWord2;

		// Now that we have computed the callsite ID, if there is no second
		// callsite we can replace its pointer with a more obvious choice,
		// such as 0x0. This is the value that will appear in the output
		// file. 
		if(callsite2 == (void *)NO_CALLSITE)
			callsite2 = (void *)0x0;

		// Store the callsite_id in the shadow memory location that
		// corresponds to the object's malloc header.
		long object_offset = (char *)objAlloc - (char *)thrData.watchStartByte;

		// Check to see whether this object is trackable/mappable to shadow
		// memory. Reasons it may not be include malloc utilizing mmap to
		// fulfill certain requests, and the heap may have possibly outgrown
		// the size of shadow memory. We only want to keep track of objects
		// that mappable to shadow memory.
		if((object_offset >= 0) &&
				((object_offset + totalObjSz) < SHADOW_MEM_SIZE)) {
			if(objAlloc > thrData.maxObjAddr) {
				// Only update the max pointer if this object could be
				// tracked, given the limited size of our shadow memory.
				thrData.maxObjAddr = (void *)((char *)objAlloc + totalObjSz);
			}

			// Record the callsite_id to the shadow memory word
			// which corresponds to the object's malloc header.
			uint64_t *id_in_shadow_mem = (uint64_t *)(thrData.shadow_mem +
					object_offset - MALLOC_HEADER_SIZE);
			*id_in_shadow_mem = callsite_id;

			Tuple * found_value;
			if(mapCallsiteStats.find(callsite_id, &found_value)) {
				found_value->numAllocs++;
				found_value->szUsed += sz;
				found_value->szTotal += totalObjSz;
			} else {
				Tuple * new_tuple =
					newTuple(callsite1, callsite2, 1, 0, 0, totalObjSz, sz, 0);
				mapCallsiteStats.insertIfAbsent(callsite_id, new_tuple);
			}
		}
	}

	void * xxcalloc(size_t nelem, size_t elsize) {
		void * ptr = NULL;
		if((ptr = malloc(nelem * elsize)))
			memset(ptr, 0, nelem * elsize);
		return ptr;
	}

	/*
	* This routine is responsible for summing the access counts for allocated
	* objects that are being tracked using shadow memory. Additionally, the
	* objects' shadow memory is zeroed out in preparation for later reuse.
	* Callers include xxfree and xxrealloc.
	*/
	void freeShadowMem(const void * ptr) {
		if(ptr == NULL)
			return;

		long object_byte_offset = (char *)ptr - thrData.watchStartByte;
		long *callsiteHeader = (long *)(thrData.shadow_mem +
				object_byte_offset - MALLOC_HEADER_SIZE);

		// This check is necessary because we cannot trust that we are in
		// control of dynamic memory, as we do not support calls to all
		// other memory-related system calls or library functions (e.g.,
		// valloc).
		if(isWordMallocHeader(callsiteHeader)) {
			// Fetch object's size from its malloc header
			const long *objHeader = (long *)ptr - 1;
			unsigned int objSize = *objHeader - 1;
			unsigned int objSizeInWords = objSize / 8;

			unsigned long callsite_id = (unsigned long) *callsiteHeader;
			if(debug) {
				fprintf(thrData.output, ">>> found malloc header @ %p, "
						"callsite_id=0x%lx, object size=%u\n", objHeader,
						callsite_id, objSize);
			}

			// Zero-out the malloc header area of the object's shadow memory
			*callsiteHeader = 0;

			int i = 0;
			long access_count = 0;
			long *next_word = callsiteHeader;
			long numWordsToVisit = objSizeInWords - 1;
			if(object_byte_offset >= SHADOW_MEM_SIZE) {
				numWordsToVisit = (SHADOW_MEM_SIZE - object_byte_offset) / 8;
			}
			for(i = 0; i < numWordsToVisit; i++) {
				next_word++;
				if(debug) {
					fprintf(thrData.output, "\t freeShadowMem: shadow mem "
							"value @ %p = 0x%lx\n", next_word, *next_word);
				}
				access_count += *next_word;
				*next_word = 0;		// zero out the word after counting it
			}
			if(debug) {
				fprintf(thrData.output, "\t freeShadowMem: finished with "
						"object @ %p, count = %ld\n", ptr, access_count);
			}

			if(callsite_id > (unsigned long)min_pos_callsite_id) {
				Tuple * map_value;
				mapCallsiteStats.find(callsite_id, &map_value);
				map_value->numAccesses += access_count;
				map_value->numFrees++;
				// Don't count the eight bytes which represent the malloc header
				map_value->szFreed += (objSize - 8);
			}
		}
	}

	void xxfree(void * ptr) {
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
				freeShadowMem(ptr);
			}
			Real::free(ptr);
		}
	}

	inline void logUnsupportedOp() {
		fprintf(thrData.output,
				"ERROR: call to unsupported memory function: %s\n",
				__FUNCTION__);
	}
	void * xxalloca(size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * xxvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	int xxposix_memalign(void **memptr, size_t alignment, size_t size) {
		logUnsupportedOp();
		return -1;
	}
	void * xxaligned_alloc(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * xxmemalign(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * xxpvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
	}

	void * xxrealloc(void * ptr, size_t sz) {
		if(!mallocInitialized) {
			if(ptr == NULL)
				return xxmalloc(sz);
			xxfree(ptr);
			return xxmalloc(sz);
		}

		if(ptr != NULL) {
			if(!shadowMemZeroedOut &&
					((ptr >= (void *)thrData.watchStartByte) &&
					(ptr <= (void *)thrData.watchEndByte))) {
				freeShadowMem(ptr);
			}
		}

		void * reptr = Real::realloc(ptr, sz);

		if(!shadowMemZeroedOut) {
			initShadowMem(reptr, sz);
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

	void writeHashMap() {
		fprintf(thrData.output, "\n\nHash map contents\n");
		fprintf(thrData.output, "------------------------------------------\n");
		fprintf(thrData.output,
				"%-18s %-18s %-20s %-20s %6s %6s %11s %11s %11s %10s %8s\n",
				"callsite1", "callsite2", "src1", "src2", "allocs", "frees",
				"freed sz", "used sz", "total sz", "avg sz", "accesses");

		HashMapX::iterator iterator;
		for(iterator = mapCallsiteStats.begin();
				iterator != mapCallsiteStats.end(); iterator++) {
			Tuple * tuple = iterator.getData();
			void * callsite1 = tuple->callsite1;
			void * callsite2 = tuple->callsite2;

			addrinfo addrInfo1, addrInfo2;
			addrInfo1 = addr2line(callsite1);
			addrInfo2 = addr2line(callsite2);

			int count = tuple->numAllocs;
			int numFreed = tuple->numFrees;
			long szFreed = tuple->szFreed;
			long usedSize = tuple->szUsed;
			long totalSize = tuple->szTotal;
			float avgSize = usedSize / (float) count;
			long totalAccesses = tuple->numAccesses;

			fprintf(thrData.output, "%-18p ", callsite1);
			fprintf(thrData.output, "%-18p ", callsite2);
			fprintf(thrData.output, "%-14s:%-5d ",
					addrInfo1.exename, addrInfo1.lineNum);
			fprintf(thrData.output, "%-14s:%-5d ",
					addrInfo2.exename, addrInfo2.lineNum);
			fprintf(thrData.output, "%6d ", count);
			fprintf(thrData.output, "%6d ", numFreed);
			fprintf(thrData.output, "%11ld ", szFreed);
			fprintf(thrData.output, "%11ld ", usedSize);
			fprintf(thrData.output, "%11ld ", totalSize);
			fprintf(thrData.output, "%10.1f ", avgSize);
			fprintf(thrData.output, "%8ld", totalAccesses);
			fprintf(thrData.output, "\n");

			free(tuple);
		}
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

	bool isWordMallocHeader(long *word) {
		Tuple * found_item;
		return (((unsigned long)*word > (unsigned long)min_pos_callsite_id) &&
				(mapCallsiteStats.find(*word, &found_item)));
	}

	void countUnfreedObjAccesses() {
		// Flipping this flag ensures that any further calls to malloc and free
		// are not tracked and will not touch shadow memory in any way.
		shadowMemZeroedOut = true;

		char *shadow_mem_end = thrData.shadow_mem + SHADOW_MEM_SIZE;
		long max_byte_offset =
			(char *)thrData.maxObjAddr - (char *)thrData.watchStartByte;
		long *sweepStart = (long *)thrData.shadow_mem;
		long *sweepEnd = (long *)(thrData.shadow_mem + max_byte_offset);
		long *sweepCurrent;

		if(debug) {
			fprintf(thrData.output, "\n>>> sweepStart = %p, sweepEnd = %p\n",
				sweepStart, sweepEnd);
		}

		for(sweepCurrent = sweepStart; sweepCurrent <= sweepEnd;
				sweepCurrent++) {
			// If the current word corresponds to an object's malloc header then
			// check real header in the heap to determine the object's size. We
			// will then use this size to sum over the corresponding number of
			// words in shadow memory.
			if(isWordMallocHeader(sweepCurrent)) {
				long current_byte_offset =
					(char *)sweepCurrent - (char *)sweepStart;
				long callsite_id = *sweepCurrent;
				const long * realObjHeader =
					(long *)(thrData.watchStartByte + current_byte_offset);
				long objSizeInBytes = *realObjHeader - 1;
				long objSizeInWords = objSizeInBytes / 8;
				long access_count = 0;
				int i;

				if(debug) {
					fprintf(thrData.output, ">>> countUnfreed: found malloc "
						"header @ %p, callsite_id=0x%lx, object size=%ld (%ld "
						"words)\n", sweepCurrent, callsite_id, objSizeInBytes,
						objSizeInWords);
				}

				for(i = 0; i < objSizeInWords - 1; i++) {
					sweepCurrent++;
					if(debug) {
						if((sweepCurrent >= (long *)shadow_mem_end) &&
								((long *)(shadow_mem_end + sizeof(long)) >
								 sweepCurrent)) {
							fprintf(thrData.output, "-------------- END OF "
									"SHADOW MEMORY REGION --------------\n");
						}
						fprintf(thrData.output, "\t countUnfreed: shadow mem "
								"value @ %p = 0x%lx\n", sweepCurrent,
								*sweepCurrent);
					}
					access_count += *sweepCurrent;
				}
				if(debug) {
					fprintf(thrData.output, "\t finished with object count = "
							"%ld, sweepCurrent's next value = %p\n",
							access_count, (sweepCurrent + 1));
				}

				if(access_count > 0) {
					Tuple * map_value;
					mapCallsiteStats.find(callsite_id, &map_value);
					map_value->numAccesses += access_count;
				}
			}
		}
	}

	// Intercept thread creation
	int pthread_create(pthread_t * tid, const pthread_attr_t * attr,
			void *(*start_routine)(void *), void * arg) {
		return xthread::thread_create(tid, attr, start_routine, arg);
	}
}
