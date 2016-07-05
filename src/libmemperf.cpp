/**
 * @file libmemperf.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"

#include <dlfcn.h>
#include <malloc.h>
#include <alloca.h>

#include "memsample.h"
#include "real.hh"
#include "xthread.hh"

#define TEMP_BUF_SIZE 33554432

using namespace std;

typedef tuple<void *, void *, int, long, long, long> statTuple;

//bool insideHashMap = true;
__thread bool insideHashMap = true;
__thread bool isMainThread = false;
//bool shadowMemZeroedOut = false;
__thread bool shadowMemZeroedOut = false;
__thread char * shadow_mem;
__thread char * stackStart;
__thread char * stackEnd;
__thread char * watchStartByte;
__thread char * watchEndByte;
__thread void * maxObjAddr = (void *)0x0;
__thread FILE * output = NULL;
thread_local FreeQueue freeQueue1, freeQueue2;

// This map will go from the callsite id key to a tuple containing:
//	(1) the first callsite
//	(2) the second callsite
//	(3) the total number of allocations originating from this callsite pair
//	(4) the total size of these allocations (i.e., when using glibc malloc,
//		header size plus usable size)
//	(5) the used size of these allocations (i.e., a sum of sz from malloc(sz)
//		calls)
//	(6) the total number of accesses on objects spawned from this callsite id
unordered_map<uint64_t, statTuple> mapCallsiteStats;

void * program_break;
extern void * __libc_stack_end;
extern char * program_invocation_name;
extern int numSamples;
extern int numSignals;

typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main;

extern "C" {
	struct addr2line_info {
		char exename[256];
		unsigned int lineNum;
		bool error = false;
	};

	// Function prototypes
	bool isWordMallocHeader(long *word);
	size_t getTotalAllocSize(size_t sz);
	struct addr2line_info addr2line(void * ddr);
	void countUnfreedObjAccesses();
	void exitHandler();
	void freeShadowMem(void * ptr);
	void initShadowMem(void * objAlloc, size_t sz);
	void processFreeQueue();
	void writeHashMap();
	inline void getCallsites(void **callsite1, void **callsite2);

	// Function aliases
	void free(void *) __attribute__ ((weak, alias("xxfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("xxcalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("xxmalloc")));
	void * realloc(void *, size_t) __attribute__ ((weak, alias("xxrealloc")));
	void * valloc(size_t) __attribute__ ((weak, alias("xxvalloc")));
	int posix_memalign(void **, size_t, size_t) __attribute__ ((weak, alias("xxposix_memalign")));
	void * aligned_alloc(size_t, size_t) __attribute__ ((weak, alias("xxaligned_alloc")));
	void * memalign(size_t, size_t) __attribute__ ((weak, alias("xxmemalign")));
	void * pvalloc(size_t) __attribute__ ((weak, alias("xxpvalloc")));
	void * alloca(size_t) __attribute__ ((weak, alias("xxalloca")));
}
bool mallocInitialized = false;
bool hashmapInitialized = false;
char * tmpbuf;
unsigned int tmppos = 0;
unsigned int numTempAllocs = 0;
const bool debug = false;

__attribute__((constructor)) void initializer() {
	// Ensure we are operating on a system using 64-bit pointers.
	// We need to do this because we will later be taking the low 8-byte word
	// of callsites. This could obviously be expanded to support 32-bit systems
	// as well, in the future.
	size_t ptrSize = sizeof(void *);
	if(ptrSize != 8) {
		fprintf(stderr, "error: unsupported pointer size: %zu\n", ptrSize);
		abort();
	}

    // Allocate memory for our pre-init temporary allocator
    if((tmpbuf = (char *)mmap(NULL, TEMP_BUF_SIZE, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
        perror("error: unable to allocate temporary memory");
		abort();
    }

	freeQueue1 = queue<freeReq>();
	freeQueue2 = queue<freeReq>();

	Real::initializer();
	mallocInitialized = true;
	isMainThread = true;
	insideHashMap = false;
	program_break = sbrk(0);
	stackStart = (char *)__builtin_frame_address(0);
	stackEnd = stackStart + FIVE_MB;
	watchStartByte = (char *)program_break;
	watchEndByte = watchStartByte + SHADOW_MEM_SIZE;

	// Allocate shadow memory
	if((shadow_mem = (char *)mmap(NULL, SHADOW_MEM_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		fprintf(stderr, "error: unable to allocate shadow memory: %s\n", strerror(errno));
		abort();
	}

	// Generate the name of our output file, then open it for writing.
	char outputFile[MAX_FILENAME_LEN];
	snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmemperf.txt",
		program_invocation_name);
	// Will overwrite current file; change the fopen flag to "a" for append.
	output = fopen(outputFile, "w");
	if(output == NULL) {
		fprintf(stderr, "error: unable to open output file to write\n");
		return;
	}

	fprintf(output, ">>> shadow memory allocated for main thread @ %p ~ %p "
			"(size=%d bytes)\n", shadow_mem, shadow_mem + SHADOW_MEM_SIZE,
			SHADOW_MEM_SIZE);
	fprintf(output, ">>> stack start @ %p, stack+5MB = %p\n",
			stackStart, stackEnd);
	fprintf(output, ">>> watch start @ %p, watch end @ %p\n",
			watchStartByte, watchEndByte);
	fprintf(output, ">>> program break @ %p\n\n", program_break);
}

__attribute__((destructor)) void finalizer() {
	fclose(output);
}

void exitHandler() {
	stopSampling();

	// Call processFreeQueue twice in order to ensure both queues containing
	// free() requests have been purged of their contents prior to exiting.
	processFreeQueue();
	processFreeQueue();

	fprintf(output, ">>> numSamples = %d, numSignals = %d\n", numSamples, numSignals);
	countUnfreedObjAccesses();
	writeHashMap();
}

// MemPerf's main function
int libmemperf_main(int argc, char ** argv, char ** envp) {
	// Immediately following execution of the class constructor, global
	// variables (including the hash map) will be initialized. Therefore,
	// we will now indicate the hash map's readiness.
	hashmapInitialized = true;

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
	struct stack_frame {
		struct stack_frame * prev;	// pointing to previous stack_frame
		void * caller_address;		// the address of caller
	};

	void processFreeQueue() {
		// Process any items in queue1 first.
		while(!freeQueue1.empty()) {
			freeReq free_req = freeQueue1.front();
			freeShadowMem(free_req.ptr);
			Real::free(free_req.ptr);
			freeQueue1.pop();
		}

		// Move anything from queue2 into queue1 to be processed next time.
		while(!freeQueue2.empty()) {
			freeReq free_req = freeQueue2.front();
			freeQueue1.push(free_req);
			freeQueue2.pop();
		}
	}

	void * xxmalloc(size_t sz) {
		// Small allocation routine designed to service malloc requests made by the
		// dlsym() function. Due to our linkage aliasing which redirects calls to
		// malloc to xxmalloc in this file, when dlsym calls malloc, xxmalloc is
		// called instead. xxmalloc then relies on Real::malloc to fulfill the
		// request, however, Real::malloc has not yet been assigned until the dlsym
		// call returns. This results in a segmentation fault. To remedy the problem,
		// we detect whether Real has finished initializing; if it has not, we
		// fulfill malloc requests using mmap regions. Once dlsym finishes,
		// all future malloc requests will be fulfilled by Real::malloc, which
		// contains a reference to the real libc malloc routine.
		if(!mallocInitialized) {
			if(tmppos + sz < TEMP_BUF_SIZE) {
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

		if(hashmapInitialized && !insideHashMap && !shadowMemZeroedOut) {
			initShadowMem(objAlloc, sz);
		}

		return objAlloc;
	}

	/*
	* This routine is responsible for computing a callsite ID based on the two
	* callsite parameters, then writing this ID to the shadow memory which
	* corresponds to the given object's malloc header. Additionally, this
	* allocation is recorded in the global stats hash map.
	* Callers include xxmalloc and xxrealloc.
	*/
	void initShadowMem(void * objAlloc, size_t sz) {
		insideHashMap = true;

		void * callsite1;
		void * callsite2;
		getCallsites(&callsite1, &callsite2);

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

		if(((char *)objAlloc + totalObjSz) > maxObjAddr) {
			long access_byte_offset = (char *)objAlloc - (char *)watchStartByte;
			// Only update the maxObjAddr variable if this object could be
			// tracked given the limited size of shadow memory.
			if(access_byte_offset >= 0 && access_byte_offset < SHADOW_MEM_SIZE)
				maxObjAddr = (void *)((char *)objAlloc + totalObjSz);
		}

		// Store the callsite_id in the shadow memory location that
		// corresponds to the object's malloc header.
		long object_offset = (char *)objAlloc - (char *)watchStartByte;

		// Check to see whether this object is trackable/mappable to shadow
		// memory. Reasons it may not be include malloc utilizing mmap to
		// fulfill certain requests, and the heap may have possibly outgrown
		// the size of shadow memory. We only want to keep track of objects
		// that mappable to shadow memory.
		if(object_offset >= 0 && object_offset < SHADOW_MEM_SIZE) {
			if(objAlloc > maxObjAddr) {
				// Only update the max pointer if this object could be
				// tracked, given the limited size of our shadow memory.
				if(object_offset >= 0 && object_offset < SHADOW_MEM_SIZE)
					maxObjAddr = (void *)((char *)objAlloc + totalObjSz);
			}

			// Record the callsite_id to the shadow memory word
			// which corresponds to the object's malloc header.
			uint64_t *id_in_shadow_mem = (uint64_t *)(shadow_mem + object_offset - MALLOC_HEADER_SIZE);
			*id_in_shadow_mem = callsite_id;

			// Search the hash map to determine whether
			// we already have a record of this callsite ID.
			unordered_map<uint64_t, statTuple>::iterator search_map_for_id =
				mapCallsiteStats.find(callsite_id);

			// If we found a match for this callsite_id
			// then update its tuple in the hash map...
			if(search_map_for_id != mapCallsiteStats.end()) {
				statTuple found_value = search_map_for_id->second;
				int oldCount = get<2>(found_value);
				long oldUsedSize = get<3>(found_value);
				long oldTotalSize = get<4>(found_value);
				long oldAccessCount = get<5>(found_value);
				mapCallsiteStats[callsite_id] = make_tuple(callsite1, callsite2,
						(oldCount+1), (oldUsedSize+sz), (oldTotalSize+totalObjSz),
						oldAccessCount);
			} else {
				// Otherwise, if we did not find a match for this callsite_id
				// then create and insert a new tuple into the hash map...
				statTuple new_value(make_tuple(callsite1,
							callsite2, 1, sz, totalObjSz, 0));
				mapCallsiteStats.insert({callsite_id, new_value});
			}
		}
		insideHashMap = false;
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
	void freeShadowMem(void * ptr) {
		if(ptr == NULL)
			return;

		long shadow_mem_offset = (char *)ptr - (char *)watchStartByte;
		long *callsiteHeader = (long *)(shadow_mem + shadow_mem_offset - MALLOC_HEADER_SIZE);

		// This check is necessary because we cannot trust that we are in
		// control of dynamic memory, as we do not support calls to other
		// memory-related system or library functions (e.g., valloc).
		if(isWordMallocHeader(callsiteHeader)) {
			// Fetch object's size from its malloc header
			long *objHeader = (long *)ptr - 1;
			unsigned int objSize = *objHeader - 1;
			unsigned int objSizeInWords = objSize / 8;

			long callsite_id = *callsiteHeader;
			if(debug) {
				fprintf(output, ">>> found malloc header @ %p, callsite_id=0x%lx, object size=%u\n",
						objHeader, callsite_id, objSize);
			}

			// Zero-out the malloc header area of the object's shadow memory.
			*callsiteHeader = 0;

			int i = 0;
			long access_count = 0;
			long *next_word = callsiteHeader;
			for(i = 0; i < objSizeInWords - 1; i++) {
				next_word++;
				if(debug)
					fprintf(output, "\t shadow mem value @ %p = 0x%lx\n", next_word, *next_word);
				access_count += *next_word;
				*next_word = 0;		// zero out the word after counting it
			}
			if(debug) {
				fprintf(output, "\t finished with object count = %ld of ptr @ %p\n",
						access_count, ptr);
			}

			if(((unsigned long)callsite_id > (unsigned long)LOWEST_POS_CALLSITE_ID) &&
					(access_count > 0)) {
				insideHashMap = true;
				statTuple map_value = mapCallsiteStats[callsite_id];
				void *callsite1 = get<0>(map_value);
				void *callsite2 = get<1>(map_value);
				int count = get<2>(map_value);
				long usedSize = get<3>(map_value);
				long totalSize = get<4>(map_value);
				long totalAccesses = get<5>(map_value);
				mapCallsiteStats[callsite_id] = make_tuple(callsite1, callsite2,
						count, usedSize, totalSize, (totalAccesses + access_count));
				insideHashMap = false;
			}
		}
	}

	void xxfree(void * ptr) {
		if(ptr == NULL)
			return;

		// Determine whether the specified object came from our global buffer;
		// only call Real::free() if the object did not come from here.
		if((ptr >= (void *)tmpbuf) && (ptr <= (void *)(tmpbuf + TEMP_BUF_SIZE))) {
			numTempAllocs--;
			if(numTempAllocs == 0) {
				if(mallocInitialized)
					munmap(tmpbuf, TEMP_BUF_SIZE);
				else
					tmppos = 0;
			}
		} else {
			if(hashmapInitialized && !shadowMemZeroedOut && !insideHashMap &&
					(ptr >= (void *)watchStartByte && ptr <= (void *)watchEndByte)) {
				freeReq free_req = {ptr};
				freeQueue2.push(free_req);
			} else {
				Real::free(ptr);
			}
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
			if(hashmapInitialized && !shadowMemZeroedOut && !insideHashMap &&
					(ptr >= (void *)watchStartByte && ptr <= (void *)watchEndByte)) {
				freeShadowMem(ptr);
			}
		}

		void * reptr = Real::realloc(ptr, sz);

		if(hashmapInitialized && !insideHashMap && !shadowMemZeroedOut) {
			initShadowMem(reptr, sz);
		}

		return reptr;
	}

	inline void getCallsites(void **callsite1, void **callsite2) {
		struct stack_frame * current_frame =
			(struct stack_frame *)(__builtin_frame_address(1));
		*callsite1 = current_frame->caller_address;
		struct stack_frame * prev_frame = current_frame->prev;
		// We cannot assume that a previous stack frame exists; doing so, and then
		// attempting to dereference its address will result in a segfault.
		// Therefore, we first determine whether its address is a valid stack
		// address. If so, then we proceed by deferencing it. If it is NOT a
		// stack address, then we will use NO_CALLSITE as a placeholder value.
		*callsite2 = (void *)NO_CALLSITE;
		if(((void *)prev_frame <= (void *)stackStart) &&
				(prev_frame >= current_frame)) {
			*callsite2 = prev_frame->caller_address;
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
		insideHashMap = true;

		fprintf(output, "Hash map contents\n");
		fprintf(output, "------------------------------------------\n");
		fprintf(output, "%-18s %-18s %-20s %-20s %6s %8s %8s %8s %8s\n",
				"callsite1", "callsite2", "src1", "src2",
				"allocs", "used sz", "total sz", "avg sz", "accesses");

		for(const auto& level1_entry : mapCallsiteStats) {
			auto& value = level1_entry.second;
			void * callsite1 = get<0>(value);
			void * callsite2 = get<1>(value);

			struct addr2line_info addrInfo1, addrInfo2;
			addrInfo1 = addr2line(callsite1);
			addrInfo2 = addr2line(callsite2);

			int count = get<2>(value);
			long usedSize = get<3>(value);
			long totalSize = get<4>(value);
			float avgSize = usedSize / (float) count;
			long totalAccesses = get<5>(value);

			fprintf(output, "%-18p ", callsite1);
			fprintf(output, "%-18p ", callsite2);
			fprintf(output, "%-14s:%-5d ", addrInfo1.exename, addrInfo1.lineNum);
			fprintf(output, "%-14s:%-5d ", addrInfo2.exename, addrInfo2.lineNum);
			fprintf(output, "%6d ", count);
			fprintf(output, "%8ld ", usedSize);
			fprintf(output, "%8ld ", totalSize);
			fprintf(output, "%8.1f ", avgSize);
			fprintf(output, "%8ld", totalAccesses);
			fprintf(output, "\n");
		}
		insideHashMap = false;
	}

	struct addr2line_info addr2line(void * addr) {
		int fd[2];
		char strCallsite[16];
		char strInfo[512];
		struct addr2line_info info;

		if(pipe(fd) == -1) {
			fprintf(stderr, "error: unable to create pipe\n");
			strcpy(info.exename, "error");
			info.lineNum = 0;
			info.error = true;
			return info;
		}

		switch(fork()) {
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
		long access_byte_offset = (char *)word - shadow_mem;
		long access_word_offset = access_byte_offset / WORD_SIZE;

		/*
		// DEBUG BLOCK
		bool bGreater = ((unsigned long)*word > (unsigned long)LOWEST_POS_CALLSITE_ID);
		bool isMod = (access_word_offset % 2 == 1);
		int iCount = mapCallsiteStats.count(*word);
		if(!(bGreater && isMod && (iCount > 0))) {
			fprintf(output, " >> %p=0x%lx not a malloc header: bGreater=%d, isMod=%d, iCount=%d\n",
				word, *word, bGreater, isMod, iCount);
		}
		*/

		return (((unsigned long)*word > (unsigned long)LOWEST_POS_CALLSITE_ID) &&
				(access_word_offset % 2 == 1) &&
				(mapCallsiteStats.count(*word) > 0));
	}

	void countUnfreedObjAccesses() {
		shadowMemZeroedOut = true;

		long max_byte_offset = (char *)maxObjAddr - (char *)watchStartByte;
		long *sweepStart = (long *)shadow_mem;
		long *sweepEnd = (long *)(shadow_mem + max_byte_offset);
		long *sweepCurrent;

		if(debug)
			fprintf(output, ">>> sweepStart = %p, sweepEnd = %p\n", sweepStart, sweepEnd);

		for(sweepCurrent = sweepStart; sweepCurrent <= sweepEnd; sweepCurrent++) {
			// If the current word corresponds to an object's malloc header then check
			// real header in the heap to determine the object's size. We will then
			// use this size to sum over the corresponding number of words in shadow
			// memory.
			if(isWordMallocHeader(sweepCurrent)) {
				long current_byte_offset = (char *)sweepCurrent - (char *)sweepStart;
				long callsite_id = *sweepCurrent;
				long *realObjHeader = (long *)(watchStartByte + current_byte_offset);
				long objSizeInBytes = *realObjHeader - 1;
				long objSizeInWords = objSizeInBytes / 8;
				long access_count = 0;
				int i;

				if(debug) {
					fprintf(output, ">>> found malloc header @ %p, "
						"callsite_id=0x%lx, object size=%ld (%ld words)\n",
						sweepCurrent, callsite_id, objSizeInBytes,
						objSizeInWords);
				}

				for(i = 0; i < objSizeInWords - 1; i++) {
					sweepCurrent++;
					if(debug) {
						fprintf(output, "\t shadow mem value @ %p = 0x%lx\n",
							sweepCurrent, *sweepCurrent);
					}
					access_count += *sweepCurrent;
				}
				if(debug) {
					fprintf(output, "\t finished with object count = %ld, "
						"sweepCurrent's next value = %p\n", access_count,
						(sweepCurrent + 1));
				}

				if(access_count > 0) {
					insideHashMap = true;
					statTuple map_value = mapCallsiteStats[callsite_id];
					void *callsite1 = get<0>(map_value);
					void *callsite2 = get<1>(map_value);
					int count = get<2>(map_value);
					long usedSize = get<3>(map_value);
					long totalSize = get<4>(map_value);
					long totalAccesses = get<5>(map_value);
					mapCallsiteStats[callsite_id] = make_tuple(callsite1,
						callsite2, count, usedSize, totalSize,
						(totalAccesses + access_count));
					insideHashMap = false;
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
