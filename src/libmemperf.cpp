/**
 * @file libmemperf.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"

#include <dlfcn.h>
#include <malloc.h>
#include <alloca.h>
#include <sys/wait.h>

#include "memsample.h"
#include "real.hh"
#include "xthread.hh"
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "spinlock.hh"

typedef HashMap<uint64_t, Tuple *, spinlock> HashMapX;

__thread bool isMainThread = false;
__thread char * shadow_mem;
__thread char * stackStart;
__thread char * stackEnd;
__thread char * watchStartByte;
__thread char * watchEndByte;
__thread void * maxObjAddr = (void *)0x0;
__thread FILE * output = NULL;
__thread FreeQueue * freeQueue1, * freeQueue2;

// This map will go from the callsite ID key to a structure containing:
//	(1) the first callsite
//	(2) the second callsite
//	(3) the total number of allocations originating from this callsite pair
//	(4) the total size of these allocations (i.e., when using glibc malloc,
//		header size plus usable size)
//	(5) the used size of these allocations (i.e., a sum of sz from malloc(sz)
//		calls)
//	(6) the total number of accesses on objects spawned from this callsite id
HashMapX mapCallsiteStats;

extern char data_start;
extern char _etext;
extern char * program_invocation_name;
extern void * __libc_stack_end;
extern char __executable_start;
extern int numSamples;
extern int numSignals;

typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main;

bool const debug = false;
bool mallocInitialized = false;
bool shadowMemZeroedOut = false;
char * tmpbuf;
unsigned int numTempAllocs = 0;
unsigned int tmppos = 0;
uint64_t min_pos_callsite_id;

struct stack_frame {
	struct stack_frame * prev;	// pointing to previous stack_frame
	void * caller_address;		// the address of caller
};

extern "C" {
	// Function prototypes
	addrinfo addr2line(void * ddr);
	FreeQueue * newFreeQueue();
	QueueItem FreeDequeue(FreeQueue * queue);
	bool isFreeQueueEmpty(FreeQueue * queue);
	bool isWordMallocHeader(long *word);
	size_t getTotalAllocSize(size_t sz);
	void countUnfreedObjAccesses();
	void exitHandler();
	void FreeEnqueue(FreeQueue * queue, QueueItem item);
	void freeShadowMem(const void * ptr);
	void initShadowMem(const void * objAlloc, size_t sz);
    void processFreeQueue();
	void writeHashMap();
	inline void getCallsites(void **callsites);

	// Function aliases
	void free(void *) __attribute__ ((weak, alias("xxfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("xxcalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("xxmalloc")));
	void * realloc(void *, size_t) __attribute__ ((weak, alias("xxrealloc")));
	void * valloc(size_t) __attribute__ ((weak, alias("xxvalloc")));
	void * aligned_alloc(size_t, size_t) __attribute__ ((weak, alias("xxaligned_alloc")));
	void * memalign(size_t, size_t) __attribute__ ((weak, alias("xxmemalign")));
	void * pvalloc(size_t) __attribute__ ((weak, alias("xxpvalloc")));
	void * alloca(size_t) __attribute__ ((weak, alias("xxalloca")));
	int posix_memalign(void **, size_t, size_t) __attribute__ ((weak, alias("xxposix_memalign")));
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
	isMainThread = true;
	void * program_break = sbrk(0);
	stackStart = (char *)__builtin_frame_address(0);
	stackEnd = (char *)__libc_stack_end;
	watchStartByte = (char *)program_break;
	watchEndByte = watchStartByte + SHADOW_MEM_SIZE - 1;

	// Initialize hashmap
	mapCallsiteStats.initialize(HashFuncs::hashCallsiteId, HashFuncs::compareCallsiteId, 4096);

	// Initialize free queues.
	freeQueue1 = newFreeQueue();
	freeQueue2 = newFreeQueue();

	// Allocate shadow memory
	if((shadow_mem = (char *)mmap(NULL, SHADOW_MEM_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		fprintf(stderr, "error: unable to allocate shadow memory: %s\n", strerror(errno));
		abort();
	}

	// Generate the name of our output file, then open it for writing.
	char outputFile[MAX_FILENAME_LEN];
	pid_t pid = getpid();
	snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmemperf_pid_%d_tid_%d.txt",
		program_invocation_name, pid, pid);
	// Will overwrite current file; change the fopen flag to "a" for append.
	output = fopen(outputFile, "w");
	if(output == NULL) {
		fprintf(stderr, "error: unable to open output file to write\n");
		return;
	}

	fprintf(output, ">>> shadow memory allocated for main thread @ %p ~ %p "
			"(size=%ld bytes)\n", shadow_mem, shadow_mem + SHADOW_MEM_SIZE,
			SHADOW_MEM_SIZE);
	fprintf(output, ">>> stack start @ %p, stack+5MB = %p\n",
			stackStart, stackEnd);
	fprintf(output, ">>> watch start @ %p, watch end @ %p\n",
			watchStartByte, watchEndByte);
	fprintf(output, ">>> program break @ %p\n", program_break);
	fflush(output);
}

__attribute__((destructor)) void finalizer() {
	fclose(output);
}

void exitHandler() {
	stopSampling();

	//processFreeQueue();
	//processFreeQueue();

	long access_byte_offset = (char *)maxObjAddr - watchStartByte;
	char * shadow_mem_end = shadow_mem + SHADOW_MEM_SIZE;
	char * maxShadowObjAddr = shadow_mem + access_byte_offset;
	fprintf(output, ">>> numSamples = %d, numSignals = %d\n", numSamples,
		numSignals);
	fprintf(output, ">>> heap memory used = %ld bytes\n", access_byte_offset);
	if(maxShadowObjAddr >= shadow_mem_end) {
		fprintf(output, ">>> WARNING: shadow memory was exceeded! "
			"maxObjAddr = %p (shadow: %p)\n\n", maxObjAddr, maxShadowObjAddr);
	}
	fflush(output);
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

extern "C" int __libc_start_main(main_fn_t, int, char **, void (*)(), void (*)(), void (*)(), void *) __attribute__((weak, alias("libmemperf_libc_start_main")));

extern "C" int libmemperf_libc_start_main(main_fn_t main_fn, int argc, char ** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(), void * stack_end) {
	auto real_libc_start_main = (decltype(__libc_start_main) *)dlsym(RTLD_NEXT, "__libc_start_main");
	real_main = main_fn;
	return real_libc_start_main(libmemperf_main, argc, argv, init, fini, rtld_fini, stack_end);
}

// Memory management functions
extern "C" {
	void processFreeQueue() {
        // Process all items in queue1 first.
        while(!isFreeQueueEmpty(freeQueue1)) {
            void * free_req = FreeDequeue(freeQueue1);
            freeShadowMem((const void *)free_req);
            Real::free(free_req);
        }

		freeQueue1 = freeQueue2;
		freeQueue2 = freeQueue1;
    }

	QueueItem FreeDequeue(FreeQueue * queue) {
		if(queue->head == NULL)
			return (QueueItem)NULL;
		QueueNode * oldHead = queue->head;
		QueueItem item = oldHead->item;
		queue->head = oldHead->next;
		Real::free(oldHead);
		return item;
	}

	void FreeEnqueue(FreeQueue * queue, QueueItem item) {
		QueueNode * node = (QueueNode *)Real::malloc(sizeof(QueueNode));

		if(queue->tail == NULL) {
			queue->head = node;
			queue->tail = node;
		} else {
			queue->tail->next = node;
			queue->tail = node;
		}

		node->next = NULL;
		node->item = item;
	}

	bool isFreeQueueEmpty(FreeQueue * queue) {
		return (queue->head == NULL);
	}

	FreeQueue * newFreeQueue() {
		FreeQueue * new_queue = (FreeQueue *)Real::malloc(sizeof(FreeQueue));
		new_queue->head = NULL;
		new_queue->tail = NULL;
		return new_queue;
	}

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
		long object_offset = (char *)objAlloc - (char *)watchStartByte;

		// Check to see whether this object is trackable/mappable to shadow
		// memory. Reasons it may not be include malloc utilizing mmap to
		// fulfill certain requests, and the heap may have possibly outgrown
		// the size of shadow memory. We only want to keep track of objects
		// that mappable to shadow memory.
		if(object_offset >= 0 && (object_offset + totalObjSz) < SHADOW_MEM_SIZE) {
			if(objAlloc > maxObjAddr) {
				// Only update the max pointer if this object could be
				// tracked, given the limited size of our shadow memory.
				maxObjAddr = (void *)((char *)objAlloc + totalObjSz);
			}

			// Record the callsite_id to the shadow memory word
			// which corresponds to the object's malloc header.
			uint64_t *id_in_shadow_mem = (uint64_t *)(shadow_mem + object_offset - MALLOC_HEADER_SIZE);
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

		long object_byte_offset = (char *)ptr - watchStartByte;
		long *callsiteHeader = (long *)(shadow_mem +
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
				fprintf(output, ">>> found malloc header @ %p, "
						"callsite_id=0x%lx, object size=%u\n", objHeader,
						callsite_id, objSize);
			}

			// Zero-out the malloc header area of the object's shadow memory.
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
					fprintf(output, "\t freeShadowMem: shadow mem value @ %p "
							"= 0x%lx\n", next_word, *next_word);
				}
				access_count += *next_word;
				*next_word = 0;		// zero out the word after counting it
			}
			if(debug) {
				fprintf(output, "\t freeShadowMem: finished with object @ %p, "
						"count = %ld\n", ptr, access_count);
			}

			if(callsite_id > (unsigned long)min_pos_callsite_id) {
				Tuple * map_value;
				mapCallsiteStats.find(callsite_id, &map_value);
				map_value->numAccesses += access_count;
				map_value->numFrees++;
				// Subtract eight bytes representing the malloc header
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
					((ptr >= (void *)watchStartByte) &&
					(ptr <= (void *)watchEndByte))) {
				freeShadowMem(ptr);
			}
			Real::free(ptr);
		}
	}

	void * xxalloca(size_t size) {
		fprintf(output, "ERROR: call to unsupported memory function %s\n", __FUNCTION__);
		return NULL;
	}
	void * xxvalloc(size_t size) {
		fprintf(output, "ERROR: call to unsupported memory function %s\n", __FUNCTION__);
		return NULL;
	}
	int xxposix_memalign(void **memptr, size_t alignment, size_t size) {
		fprintf(output, "ERROR: call to unsupported memory function %s\n", __FUNCTION__);
		return -1;
	}
	void * xxaligned_alloc(size_t alignment, size_t size) {
		fprintf(output, "ERROR: call to unsupported memory function %s\n", __FUNCTION__);
		return NULL;
	}
	void * xxmemalign(size_t alignment, size_t size) {
		fprintf(output, "ERROR: call to unsupported memory function %s\n", __FUNCTION__);
		return NULL;
	}
	void * xxpvalloc(size_t size) {
		fprintf(output, "ERROR: call to unsupported memory function %s\n", __FUNCTION__);
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
					((ptr >= (void *)watchStartByte) &&
					(ptr <= (void *)watchEndByte))) {
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
		void * btext = &__executable_start;
		void * etext = &data_start;

		struct stack_frame * current_frame =
			(struct stack_frame *)(__builtin_frame_address(0));
		struct stack_frame * prev_frame = current_frame;

		// Initialize the array elements.
		callsites[0] = (void *)NULL;
		callsites[1] = (void *)NULL;

		int i = 0;
		while((i < 2) && ((void *)prev_frame <= (void *)stackStart) &&
				(prev_frame >= current_frame)) {
			void * caller_addr = prev_frame->caller_address;
			if((caller_addr >= btext) && (caller_addr <= etext)) {
				callsites[i++] = caller_addr;
			}
			prev_frame = prev_frame->prev;
		}
	}

	size_t getTotalAllocSize(size_t sz) {
		size_t totalSize, usableSize;

		// Smallest possible total object size is 32 bytes, thus total usable
		// object size is 32 - 8 = 24 bytes.
		if(sz <= 16)
			return 24;

		// Calculate a total object size that is double-word aligned.
		if((sz + 8) % 16 != 0) {
			totalSize = 16 * (((sz + 8) / 16) + 1);
			usableSize = totalSize - 8;
		} else {
			usableSize = sz;
		}

		return usableSize;
	}

	void writeHashMap() {
		fprintf(output, "Hash map contents\n");
		fprintf(output, "--------------------------------------------------\n");
		fprintf(output,
				"%-18s %-18s %-20s %-20s %6s %6s %8s %8s %8s %10s %8s\n",
				"callsite1", "callsite2", "src1", "src2", "allocs", "frees",
				"freed sz", "used sz", "total sz", "avg sz", "accesses");

		HashMapX::iterator iterator;
		for(iterator = mapCallsiteStats.begin();
				iterator != mapCallsiteStats.end(); iterator++) {
			Tuple * value = iterator.getData();
			void * callsite1 = value->callsite1;
			void * callsite2 = value->callsite2;

			addrinfo addrInfo1, addrInfo2;
			addrInfo1 = addr2line(callsite1);
			addrInfo2 = addr2line(callsite2);

			int count = value->numAllocs;
			int numFreed = value->numFrees;
			long szFreed = value->szFreed;
			long usedSize = value->szUsed;
			long totalSize = value->szTotal;
			float avgSize = usedSize / (float) count;
			long totalAccesses = value->numAccesses;

			fprintf(output, "%-18p ", callsite1);
			fprintf(output, "%-18p ", callsite2);
			fprintf(output, "%-14s:%-5d ", addrInfo1.exename, addrInfo1.lineNum);
			fprintf(output, "%-14s:%-5d ", addrInfo2.exename, addrInfo2.lineNum);
			fprintf(output, "%6d ", count);
			fprintf(output, "%6d ", numFreed);
			fprintf(output, "%8ld ", szFreed);
			fprintf(output, "%8ld ", usedSize);
			fprintf(output, "%8ld ", totalSize);
			fprintf(output, "%10.1f ", avgSize);
			fprintf(output, "%8ld", totalAccesses);
			fprintf(output, "\n");

			free(value);
		}
	}

	addrinfo addr2line(void * addr) {
		int fd[2];
		char strCallsite[16];
		char strInfo[512];
		addrinfo info;

		if(pipe(fd) == -1) {
			fprintf(stderr, "error: unable to create pipe\n");
			strcpy(info.exename, "error");
			info.lineNum = 0;
			info.error = true;
			return info;
		}

		pid_t parent;
		switch(parent = fork()) {
			case -1:
				fprintf(stderr, "error: unable to fork child process\n");
				break;
			case 0:		// child
				close(fd[0]);
				dup2(fd[1], STDOUT_FILENO);
				sprintf(strCallsite, "%p", addr);
				execlp("addr2line", "addr2line", "-s", "-e", program_invocation_name,
					strCallsite, (char *)NULL);
				exit(EXIT_FAILURE);		// if we're still here, then exec failed
				break;
			default:	// parent
				close(fd[1]);
				if(read(fd[0], strInfo, 512) == -1) {
					fprintf(stderr, "error: unable to read from pipe\n");
					strcpy(info.exename, "error");
					info.lineNum = 0;
					info.error = true;
					return info;
				}
				close(fd[0]);
				waitpid(parent, NULL, 0);

				// Tokenize the return string, breaking apart by ':'
				// Take the second token, which will be the line number.
				char * token = strtok(strInfo, ":");
				strncpy(info.exename, token, 256);
				token = strtok(NULL, ":");
				info.lineNum = atoi(token);
		}

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

		char *shadow_mem_end = shadow_mem + SHADOW_MEM_SIZE;
		long max_byte_offset = (char *)maxObjAddr - (char *)watchStartByte;
		long *sweepStart = (long *)shadow_mem;
		long *sweepEnd = (long *)(shadow_mem + max_byte_offset);
		long *sweepCurrent;

		if(debug) {
			fprintf(output, "\n>>> sweepStart = %p, sweepEnd = %p\n",
				sweepStart, sweepEnd);
		}

		for(sweepCurrent = sweepStart; sweepCurrent <= sweepEnd; sweepCurrent++) {
			// If the current word corresponds to an object's malloc header then check
			// real header in the heap to determine the object's size. We will then
			// use this size to sum over the corresponding number of words in shadow
			// memory.
			if(isWordMallocHeader(sweepCurrent)) {
				long current_byte_offset =
					(char *)sweepCurrent - (char *)sweepStart;
				long callsite_id = *sweepCurrent;
				const long * realObjHeader =
					(long *)(watchStartByte + current_byte_offset);
				long objSizeInBytes = *realObjHeader - 1;
				long objSizeInWords = objSizeInBytes / 8;
				long access_count = 0;
				int i;

				if(debug) {
					fprintf(output, ">>> countUnfreed: found malloc header @ %p"
						", callsite_id=0x%lx, object size=%ld (%ld words)\n",
						sweepCurrent, callsite_id, objSizeInBytes,
						objSizeInWords);
				}

				for(i = 0; i < objSizeInWords - 1; i++) {
					sweepCurrent++;
					if(debug) {
						if((sweepCurrent >= (long *)shadow_mem_end) &&
								((long *)(shadow_mem_end + sizeof(long)) >
								 sweepCurrent)) {
							fprintf(output, "-------------- END OF "
									"SHADOW MEMORY REGION --------------\n");
						}
						fprintf(output, "\t countUnfreed: shadow mem value @ %p "
								"= 0x%lx\n", sweepCurrent, *sweepCurrent);
					}
					access_count += *sweepCurrent;
				}
				if(debug) {
					fprintf(output, "\t finished with object count = %ld, "
						"sweepCurrent's next value = %p\n", access_count,
						(sweepCurrent + 1));
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
	int pthread_create(pthread_t * tid, const pthread_attr_t * attr, void * (*start_routine)(void *),
			void * arg) {
		return xthread::thread_create(tid, attr, start_routine, arg);
	}
}
