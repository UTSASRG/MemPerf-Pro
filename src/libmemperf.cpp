/**
 * @file libmemperf.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unordered_map>
#include <tuple>
#include <malloc.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "real.hh"
#include "xthread.hh"
#include "memsample.h"

using namespace std;

typedef tuple<void *, void *, int, long, long, long> statTuple;

// This map will go from the callsite id key to a tuple containing:
//	(1) the first callsite
//	(2) the second callsite
//	(3) the total number of allocations originating from this callsite pair
//	(4) the total size of these allocations (i.e., when using glibc malloc, header size plus usable size)
//	(5) the used size of these allocations (i.e., a sum of sz from malloc(sz) calls)
//	(6) the total number of accesses on objects spawned from this callsite id
thread_local unordered_map<uint64_t, statTuple> mapCallsiteStats;

// This map will maintain mappings between requested malloc sizes and the
// actual usable size of fulfilled requests.
thread_local unordered_map<size_t, size_t> mapRealAllocSize;

__thread bool insideHashMap = false;
__thread bool isMainThread = false;
__thread char * shadow_mem;
__thread char * stackStart;
__thread char * stackEnd;
__thread char * watchStartByte;
__thread char * watchEndByte;
__thread void * maxObjAddr = (void *)0x0;
__thread FILE * output;

void * program_break;
extern void * __libc_stack_end;
extern char * program_invocation_name;
extern int numSamples;
extern int numSignals;

typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main;

extern "C" {
	pid_t gettid() {
		return syscall(__NR_gettid);
	}

	struct addr2line_info {
		char exename[256];
		unsigned int lineNum;
		bool error = false;
	};
	void printHashMap();
	size_t getTotalAllocSize(void * objAlloc, size_t sz);
	struct addr2line_info addr2line(void * ddr);
	bool isWordMallocHeader(long *word);
	void countUnfreedObjAccesses();

	void free(void *) __attribute__ ((weak, alias("xxfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("xxcalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("xxmalloc")));

	// TODO: How to handle realloc?	-Sam
	//void * realloc(void *, size_t) __attribute__ ((weak, alias("xxrealloc")));
}
bool initialized = false;
char tmpbuf[262144];		// 256KB global buffer
unsigned int tmppos = 0;
const bool debug = true;

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

	Real::initializer();
	initialized = true;
	isMainThread = true;
	program_break = sbrk(0);
	stackStart = (char *)__builtin_frame_address(0);
	stackEnd = stackStart + FIVE_MB;
	watchStartByte = (char *)program_break;
	watchEndByte = watchStartByte + SHADOW_MEM_SIZE;

	// Allocate shadow memory
	if((shadow_mem = (char *)mmap(NULL, SHADOW_MEM_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == (void *) -1) {
		fprintf(stderr, "error: unable to allocate shadow memory: %s\n", strerror(errno));
		abort();
	}

	char outputFile[MAX_FILENAME_LEN];
	snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmemperf.txt",
		program_invocation_name);

	// Presently set to overwrite file; change fopen flag to "a" for append.
	output = fopen(outputFile, "w");
	if(output == NULL) {
		fprintf(stderr, "error: unable to open output file for writing debug data\n");
		return;
	}

	fprintf(output, ">>> shadow memory allocated for main thread @ %p ~ %p (size=%d bytes)\n", shadow_mem,
			shadow_mem + SHADOW_MEM_SIZE, SHADOW_MEM_SIZE);
	fprintf(output, ">>> stack start @ %p, stack+5MB = %p\n", stackStart, stackEnd);
	fprintf(output, ">>> watch start @ %p, watch end @ %p\n", watchStartByte, watchEndByte);
	fprintf(output, ">>> program break @ %p\n\n", program_break);
}

__attribute__((destructor)) void finalizer() {
	fclose(output);
}

// MemPerf's main function
int libmemperf_main(int argc, char ** argv, char ** envp) {
	initSampling();
	int main_ret_val = real_main(argc, argv, envp);
	stopSampling();
	fprintf(output, ">>> numSamples = %d, numSignals = %d\n", numSamples, numSignals);
	countUnfreedObjAccesses();
	printHashMap();
	return main_ret_val;
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

	void * xxmalloc(size_t sz) {
		// Small allocation routine designed to service malloc requests made by the
		// dlsym() function. Due to our linkage aliasing which redirects calls to
		// malloc to xxmalloc in this file, when dlsym calls malloc, xxmalloc is
		// called instead. xxmalloc then relies on Real::malloc to fulfill the
		// request, however, Real::malloc has not yet been assigned until the dlsym
		// call returns. This results in a segmentation fault. To remedy the problem,
		// we detect whether Real has finished initializing; if it has not, when
		// fulfill malloc requests using a small global buffer. Once dlsym finishes,
		// all future malloc requests will be fulfilled by Real::malloc, which
		// contains a reference to the real libc malloc routine.  - Sam
		if(!initialized) {
			if(tmppos + sz < sizeof(tmpbuf)) {
				void * retptr = tmpbuf + tmppos;
				tmppos += sz;
				return retptr;
			} else {
				fprintf(stderr, "error: too much memory requested, sz=%zu\n", sz);
				exit(EXIT_FAILURE);
			}
		}

		if(!insideHashMap) {
			insideHashMap = true;

			struct stack_frame * current_frame =
							(struct stack_frame *)(__builtin_frame_address(0));
			void * callsite1 = current_frame->caller_address;
			struct stack_frame * prev_frame = current_frame->prev;
			// We cannot assume that a previous stack frame exists; doing so, and then
			// attempting to dereference its address will result in a segfault.
			// Therefore, we first determine whether its address is a valid stack
			// address. If so, then we proceed by deferencing it. If it is NOT a
			// stack address, then we will use NO_CALLSITE as a placeholder value.
			void * callsite2 = (void *)NO_CALLSITE;
			if(((void *)prev_frame >= (void *)stackStart) &&
					((void *)prev_frame <= (void *)current_frame))
				callsite2 = prev_frame->caller_address;

			uint64_t lowWord1 = (uint64_t) callsite1 & 0xFFFFFFFF;
			uint64_t lowWord2 = (uint64_t) callsite2 & 0xFFFFFFFF;
			uint64_t callsite_id = (lowWord1 << 32) | lowWord2;

			// Now that we have computed the callsite ID, if there is no second
			// callsite we can replace its pointer with a more obvious choice,
			// such as 0x0. This is the value that will appear in the output
			// file. 
			if(callsite2 == (void *)NO_CALLSITE)
				callsite2 = (void *)0x0;

			// Do the allocation and fetch the object's real total size.
			void * objAlloc = Real::malloc(sz);
			size_t totalObjSz = getTotalAllocSize(objAlloc, sz);

			if(objAlloc > maxObjAddr) {
				long access_byte_offset = (char *)objAlloc - (char *)watchStartByte;
				// Only update the maxObjAddr variable if this object could be
				// tracked given the limited size of shadow memory
				if(access_byte_offset >= 0 && access_byte_offset < SHADOW_MEM_SIZE)
					maxObjAddr = (void *)((char *)objAlloc + totalObjSz);
			}

			// Store the contrived callsite_id in the shadow memory location that
			// corresponds to the object's malloc header.
			long object_offset = (char *)objAlloc - (char *)watchStartByte;

            // Check to see whether this object is mappable to shadow memory. Reasons
            // it may not include malloc utilizing mmap to fulfill the request, or
            // the heap possibly having outgrown the size of shadow memory. We only
            // want to track objects that are so mappable.
            if(object_offset >= 0 && object_offset < SHADOW_MEM_SIZE) {
				if(objAlloc > maxObjAddr) {
					// Only update the maxObjAddr variable if this object could be
					// tracked given the limited size of shadow memory
					if(object_offset >= 0 && object_offset < SHADOW_MEM_SIZE)
						maxObjAddr = (void *)((char *)objAlloc + totalObjSz);
				}

				// Record the callsite_id to the shadow memory corresponding
				// to the object's malloc header.
				uint64_t *id_in_shadow_mem = (uint64_t *)(shadow_mem + object_offset - MALLOC_HEADER_SIZE);
				*id_in_shadow_mem = callsite_id;

				unordered_map<uint64_t, statTuple>::iterator search_map_for_id =
					mapCallsiteStats.find(callsite_id);

				// If we found a match on the callsite_id ...
				if(search_map_for_id != mapCallsiteStats.end()) {
					statTuple found_value = search_map_for_id->second;
					int oldCount = get<2>(found_value);
					long oldUsedSize = get<3>(found_value);
					long oldTotalSize = get<4>(found_value);
					mapCallsiteStats[callsite_id] = make_tuple(callsite1, callsite2,
							(oldCount+1), (oldUsedSize+sz), (oldTotalSize+totalObjSz), 0);
				} else {	// If we did NOT find a match on callsite_id ...
					statTuple new_value(make_tuple(callsite1,
								callsite2, 1, sz, totalObjSz, 0));
					mapCallsiteStats.insert({callsite_id, new_value});
				}
			}

			insideHashMap = false;
			return objAlloc;
		}

		return Real::malloc(sz);
	}

	// TODO need to make this function work -- copy or factor out code from
	// xxmalloc for use here (and possibly future functions such as realloc).
	void * xxcalloc(size_t nelem, size_t elsize) {
		// If Real has not yet finished initializing, fulfill the calloc request
		// ourselves by issuing appropriate calls to malloc and memset. The
		// malloc request will be fulfilled using memory from our global buffer.
		if(!initialized) {
			void * ptr = malloc(nelem * elsize);
			if(ptr)
				memset(ptr, 0, nelem * elsize);
			return ptr;
		}

		return Real::calloc(nelem, elsize);
	}

	void xxfree(void * ptr) {
		// Determine whether the specified object came from our global buffer;
		// only call Real::free() if the object did not come from here.
		if(ptr >= (void *)tmpbuf && ptr <= (void *)(tmpbuf + tmppos)) {
			fprintf(stderr, "info: freeing temp memory\n");
		} else {
			if(!insideHashMap &&
					(ptr >= (void *)watchStartByte && ptr <= (void *)watchEndByte)) {

				long shadow_mem_offset = (char *)ptr - (char *)watchStartByte;
				if(shadow_mem_offset % WORD_SIZE != 0)
					fprintf(output, ">>> free(%p) object is not word aligned!\n", ptr);

				// Fetch object's size from malloc header
				long *objHeader = (long *)ptr - 1;
				unsigned int objSize = *objHeader - 1;

				// Zero out the malloc header area manually.
				long *callsiteHeader = (long *)(shadow_mem + shadow_mem_offset - WORD_SIZE);
				*callsiteHeader = 0;

				int i = 0;
				long count = 0;
				for(i = 0; i < objSize; i += 8) {
					long *next_word = (long *)(shadow_mem + shadow_mem_offset + i);
					count += *next_word;
					*next_word = 0;		// zero out the word after counting it
				}
			}

			Real::free(ptr);
		}
	}

	// TODO: will have to handle this eventually...   - Sam
	void * xxrealloc(void * ptr, size_t sz) {
		return Real::realloc(ptr, sz);
	}

	size_t getTotalAllocSize(void * objAlloc, size_t sz) {
		size_t curSz = mapRealAllocSize[sz];
		if(curSz == 0) {
			size_t newSz = malloc_usable_size(objAlloc) + MALLOC_HEADER_SIZE;
			mapRealAllocSize[sz] = newSz;
			return newSz;
		}
		return curSz;
	}

	void printHashMap() {
		insideHashMap = true;

		fprintf(output, "Hash map contents\n");
		fprintf(output, "------------------------------------------\n");
		fprintf(output, "%-18s %-18s %5s %-100s %5s %-100s %6s %8s %8s %8s %8s\n",
				"callsite1", "callsite2", "line1", "exename1", "line2", "exename2",
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
			fprintf(output, "%5d ", addrInfo1.lineNum);
			fprintf(output, "%-100s ", addrInfo1.exename);
			fprintf(output, "%5d ", addrInfo2.lineNum);
			fprintf(output, "%-100s ", addrInfo2.exename);
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
				execlp("addr2line", "addr2line", "-e", program_invocation_name, strCallsite, (char *)NULL);
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
		return ((*word > LOWEST_POS_CALLSITE_ID) &&
				(access_word_offset % 2 == 1) &&
				mapCallsiteStats.count(*word) > 0);
	}

	void countUnfreedObjAccesses() {
		long max_byte_offset = (char *)maxObjAddr - (char *)watchStartByte;
		long *sweepStart = (long *)shadow_mem;
		long *sweepEnd = (long *)(shadow_mem + max_byte_offset);
		long *sweepCurrent;

		if(debug)
			fprintf(output, ">>> sweepStart = %p, sweepEnd = %p\n", sweepStart, sweepEnd);

		for(sweepCurrent = sweepStart; sweepCurrent <= sweepEnd; sweepCurrent++) {
			long current_byte_offset = (char *)sweepCurrent - (char *)sweepStart;

			// If the current word corresponds to an object's malloc header then check
			// real header in the heap to determine the object's size. We will then
			// use this size to sum over the corresponding number of words in shadow
			// memory.
			if(isWordMallocHeader(sweepCurrent)) {
				long callsite_id = *sweepCurrent;
				long *realObjHeader = (long *)(watchStartByte + current_byte_offset);
				long objSizeInBytes = *realObjHeader;
				long objSizeInWords = (*realObjHeader - 1) / 8;
				long access_count = 0;
				int i;

				if(debug) {
					fprintf(output, ">>> found malloc header @ %p, callsite_id=0x%lx, object size=%ld "
						"(%ld words)\n", sweepCurrent, callsite_id, objSizeInBytes,
						objSizeInWords);
				}

				for(i = 0; i < objSizeInWords-1; i++) {
					sweepCurrent++;
					access_count += *sweepCurrent;
					if(debug)
						fprintf(output, "\t shadow mem value @ %p = 0x%lx\n", sweepCurrent, *sweepCurrent);
				}
				if(debug) {
					fprintf(output, "\t finished with object count = %ld, sweepCurrent's next "
							"value = %p\n", access_count, (sweepCurrent + 1));
				}

				if(access_count > 0) {
					statTuple map_value = mapCallsiteStats[callsite_id];
					void *callsite1 = get<0>(map_value);
					void *callsite2 = get<1>(map_value);
					int count = get<2>(map_value);
					long usedSize = get<3>(map_value);
					long totalSize = get<4>(map_value);
					long totalAccesses = get<5>(map_value);
					mapCallsiteStats[callsite_id] = make_tuple(callsite1, callsite2,
							count, usedSize, totalSize, (totalAccesses + access_count));
				}
			}
		}
	}
}

// Thread functions
extern "C" {
	int pthread_create(pthread_t * tid, const pthread_attr_t * attr, void * (*start_routine)(void *),
			void * arg) {
		return xthread::thread_create(tid, attr, start_routine, arg);
	}
}
