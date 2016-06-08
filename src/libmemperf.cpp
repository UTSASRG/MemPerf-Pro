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

#define MALLOC_HEADER_SIZE (sizeof(size_t))

using namespace std;

// This map will go from id keys to a tuple containing:
//	(1) the first callsite
//	(2) the second callsite
//	(3) the total number of allocations originating from this callsite pair
//	(4) the total size of these allocations (i.e., when using glibc malloc, header size plus usable size)
//	(5) the used size of these allocations (i.e., a sum of sz from malloc(sz) calls)
thread_local std::unordered_map<uint64_t, tuple<void *, void *, int, long long, long long>> mapCallsiteStats;
thread_local std::unordered_map<size_t, size_t> mapTotalAllocSz;
__thread bool insideHashMap = false;

void * shadow_mem;

typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main;

extern char * program_invocation_name;

extern "C" {
	struct addr2line_info {
		char exename[256];
		unsigned int lineNum;
		bool error = false;
	};
	void printHashMap();
	size_t getTotalAllocSize(void * objAlloc, size_t sz);
	struct addr2line_info addr2line(void * ddr);

	void free(void *) __attribute__ ((weak, alias("xxfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("xxcalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("xxmalloc")));

	// TODO: How to handle realloc?	-Sam
	//void * realloc(void *, size_t) __attribute__ ((weak, alias("xxrealloc")));
}

bool initialized = false;
char tmpbuf[16384];		// 16KB global buffer
unsigned int tmppos = 0;
unsigned int tmpallocs = 0;

__attribute__((constructor)) void initializer() {
	// Ensure we are operating on a system using 64-bit pointers.
	size_t ptrSize = sizeof(void *);
	if(ptrSize != 8) {
		fprintf(stderr, "error: unsupported pointer size: %zu\n", ptrSize);
		abort();
	}

	Real::initializer();
	initialized = true;

	// Allocate shadow memory
	int pagesize = getpagesize();
	if((shadow_mem = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == (void *) -1) {
		fprintf(stderr, "error: unable to map shadow memory: %s\n", strerror(errno));
		abort();
	}
	printf(">>> shadow mem allocated @ %p\n", shadow_mem);
}

__attribute__((destructor)) void finalizer() { }

// MemPerf's main function
int libmemperf_main(int argc, char ** argv, char ** envp) {
	initSampling();
	int main_ret_val = real_main(argc, argv, envp);
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

	extern void * _libc_stack_end;

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
				++tmpallocs;
				return retptr;
			} else {
				fprintf(stderr, "error: too much memory requested, sz=%zu\n", sz);
				exit(EXIT_FAILURE);
			}
		}

		if(!insideHashMap) {
			insideHashMap = true;

			struct stack_frame * current_frame;
			current_frame = (struct stack_frame *)(__builtin_frame_address(0));
			void * callsite1 = current_frame->caller_address;
			current_frame = current_frame->prev;
			void * callsite2 = current_frame->caller_address;

			uint64_t lowWord1 = (uint64_t) callsite1 & 0xFFFFFFFF;
			uint64_t lowWord2 = (uint64_t) callsite2 & 0xFFFFFFFF;
			uint64_t callsite_id = (lowWord1 << 32) | lowWord2;

			// Do the allocation and fetch the object's real total size.
			void * objAlloc = Real::malloc(sz);
			size_t totalSz = getTotalAllocSize(objAlloc, sz);

			std::unordered_map<uint64_t, tuple<void *, void *, int, long long, long long>>::iterator search_map_for_id =
							mapCallsiteStats.find(callsite_id);
	
			// If we found a match on callsite_id...
			if(search_map_for_id != mapCallsiteStats.end()) {
				std::tuple<void *, void *, int, long long, long long> found_value = search_map_for_id->second;
				int oldCount = std::get<2>(found_value);
				long long oldUsedSize = std::get<3>(found_value);
				long long oldTotalSize = std::get<4>(found_value);
				mapCallsiteStats[callsite_id] = std::make_tuple(callsite1, callsite2,
					(oldCount+1), (oldUsedSize+sz), (oldTotalSize+totalSz));
			} else {	// If we did NOT find a match on callsite_id...
				std::tuple<void *, void *, int, long long, long long> new_value(std::make_tuple(callsite1,
					callsite2, 1, sz, totalSz));
				mapCallsiteStats.insert({callsite_id, new_value});
			}

			insideHashMap = false;
			return objAlloc;
		}

		return Real::malloc(sz);
	}

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
		if(ptr >= (void *)tmpbuf && ptr <= (void *)(tmpbuf + tmppos))
			fprintf(stderr, "info: freeing temp memory\n");
		else
			Real::free(ptr);
	}

	/*
	// TODO: will have to handle this eventually...   - Sam
	void * xxrealloc(void * ptr, size_t sz) {
		return Real::realloc(ptr, sz);
	}
	*/

	size_t getTotalAllocSize(void * objAlloc, size_t sz) {
		size_t curSz = mapTotalAllocSz[sz];
		if(curSz == 0) {
			size_t newSz = malloc_usable_size(objAlloc) + MALLOC_HEADER_SIZE;
			mapTotalAllocSz[sz] = newSz;
			return newSz;
		}
		return curSz;
	}

	void printHashMap() {
		char outputFile[256];
		strcpy(outputFile, program_invocation_name);
		strcat(outputFile, "_libmemperf.txt");

		// Presently set to overwrite file; change fopen flag to "a" for append.
		FILE * output = fopen(outputFile, "w");
		if(output == NULL) {
			fprintf(stderr, "error: unable to open output file for writing hash map\n");
			return;
		}

		fprintf(output, "Hash map contents:\n");
		fprintf(output, "--------------------------------\n");
		fprintf(output, "%-18s %-18s %5s %-64s %5s %-64s %6s %8s %8s %14s\n", "callsite1", "callsite2",
						"line1", "exename1", "line2", "exename2", "allocs", "used sz", "total sz", "avg sz");

		for(const auto& level1_entry : mapCallsiteStats) {
			auto& value = level1_entry.second;
			void * callsite1 = std::get<0>(value);
			void * callsite2 = std::get<1>(value);

			struct addr2line_info addrInfo1, addrInfo2;
			addrInfo1 = addr2line(callsite1);
			addrInfo2 = addr2line(callsite2);

			int count = std::get<2>(value);
			long long usedSize = std::get<3>(value);
			long long totalSize = std::get<4>(value);
			float avgSize = usedSize / (float) count;

			fprintf(output, "%-18p ", callsite1);
			fprintf(output, "%-18p ", callsite2);
			fprintf(output, "%5d ", addrInfo1.lineNum);
			fprintf(output, "%-64s ", addrInfo1.exename);
			fprintf(output, "%5d ", addrInfo2.lineNum);
			fprintf(output, "%-64s ", addrInfo2.exename);
			fprintf(output, "%6d ", count);
			fprintf(output, "%8lld ", usedSize);
			fprintf(output, "%8lld ", totalSize);
			fprintf(output, "%14.1f", avgSize);
			fprintf(output, "\n");
		}
		fprintf(output, "\n\n");
		fclose(output);
	}

	struct addr2line_info addr2line(void * addr) {
		int fd[2];
		char strCallsite[16];
		char strInfo[512];
		struct addr2line_info info;

		if(pipe(fd) == -1) {
			printf("error: unable to create pipe\n");
			strcpy(info.exename, "error");
			info.lineNum = 0;
			info.error = true;
			return info;
		}

		switch(fork()) {
			case -1:
				printf("error: unable to fork child process\n");
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
					printf("error: unable to read from pipe\n");
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
}

// Thread functions
extern "C" {
  int pthread_create(pthread_t * tid, const pthread_attr_t * attr, void * (*start_routine)(void *),
		     void * arg) {
    return xthread::thread_create(tid, attr, start_routine, arg);
  }
}
