/**
 * @file libheapperf.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"

#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstddef>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <tuple>

#include "real.hh"
#include "xthread.hh"
#include "memsample.h"

using namespace std;
enum { InitialMallocSize = 1024 * 1024 * 1024 };

__thread bool insideHashMap = false;

thread_local std::unordered_map<void*, std::unordered_map<void*, tuple<int, long long>>> memAllocCountMap;

typedef int (*main_fn_t)(int, char**, char**);
main_fn_t real_main;

extern "C" struct addr2line_info {
	char exename[256];
	unsigned int lineNum;
	bool error = false;
};
extern "C" void printHashMap();
extern "C" struct addr2line_info addr2line(void *addr);
extern char * program_invocation_name;

extern "C" {
	void free(void *) __THROW __attribute__ ((weak, alias("xxfree")));
	void *calloc(size_t, size_t) __THROW __attribute__ ((weak, alias("xxcalloc")));
	void *malloc(size_t) __THROW __attribute__ ((weak, alias("xxmalloc")));
	//void *realloc(void *, size_t) __THROW WEAK(xxrealloc);
}

char tmpbuff[100000];
unsigned long tmppos = 0;
unsigned long tmpallocs = 0;

extern "C" void * xxcalloc(size_t nelem, size_t elsize);
__attribute__((constructor)) void initializer() {
	Real::initializer();
	Real::initialized = true;
}

__attribute__((destructor)) void finalizer() { }

// MemPerf's main function
int libheapperf_main(int argc, char** argv, char** envp) {
	initSampling();

	int main_ret_val = real_main(argc, argv, envp);

	printHashMap();

  return main_ret_val;
}

extern "C" int __libc_start_main(main_fn_t, int, char**, void (*)(), void (*)(), void (*)(), void*) __attribute__((weak, alias("libheapperf_libc_start_main")));

extern "C" int libheapperf_libc_start_main(main_fn_t main_fn, int argc, char** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
  // Find the real __libc_start_main
  auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");
  // Save the program's real main function
  real_main = main_fn;
  // Run the real __libc_start_main, but pass in doubletake's main function
  return real_libc_start_main(libheapperf_main, argc, argv, init, fini, rtld_fini, stack_end);
}

// Memory management functions
extern "C" {
	struct stack_frame {
  	struct stack_frame * prev;/* pointing to previous stack_frame */
  	void* caller_address;/* the address of caller */
	};

	extern void *__libc_stack_end;

  void* xxmalloc(size_t sz) {
		//fprintf(stderr, ">>> value of Real::initialized == %d\n", Real::initialized);
		if(!Real::initialized) {
			if(tmppos + sz < sizeof(tmpbuff)) {
				void *retptr = tmpbuff + tmppos;
				tmppos += sz;
				++tmpallocs;
				return retptr;
			} else {
				fprintf(stderr, "error: too much memory requested, sz=%zu\n", sz);
				exit(1);
			}
		}

		if(!insideHashMap) {
			insideHashMap = true;

			struct stack_frame *current_frame;
			current_frame = (struct stack_frame*)(__builtin_frame_address(0));
			void *callsite1 = current_frame->caller_address;
			current_frame = current_frame->prev;
			void *callsite2 = current_frame->caller_address;

			std::unordered_map<void*, std::unordered_map<void*, tuple<int, long long>>>::iterator search_level1 =
							memAllocCountMap.find(callsite1);
	
			// If we found a match on callsite1...
			if(search_level1 != memAllocCountMap.end()) {
				std::unordered_map<void*, tuple<int, long long>> found_level1 = search_level1->second;
				std::unordered_map<void*, tuple<int, long long>>::iterator search_level2 = found_level1.find(callsite2);

				// If we found a match on callsite2...
				if(search_level2 != found_level1.end()) {
					std::tuple<int, long long> found_level2 = search_level2->second;
					int oldCount = std::get<0>(found_level2);
					long long oldSize = std::get<1>(found_level2);
					memAllocCountMap[callsite1][callsite2] = std::make_tuple((oldCount+1), (oldSize+sz));
				} else {	// If we did NOT find a match on callsite2...
					std::tuple<int, long long> new_tuple(std::make_tuple(1, sz));
					memAllocCountMap[callsite1].insert({callsite2, new_tuple});
				}
			} else {	// If we did NOT find a match on callsite1...
				std::unordered_map<void*, tuple<int, long long>> new_level2_map;
				std::tuple<int, long long> newTuple(std::make_tuple(1, sz));
				new_level2_map.insert({callsite2, newTuple});
				memAllocCountMap.insert({callsite1, new_level2_map});
			}
			insideHashMap = false;
		}

		return Real::malloc(sz);
  }

	void printHashMap() {
		char outputFile[256];
		strcpy(outputFile, program_invocation_name);
		strcat(outputFile, "_libheapperf.txt");

		FILE *output = fopen(outputFile, "a");
		if(output == NULL) {
			fprintf(stderr, "ERROR: unable to open output file for writing hash map\n");
			return;
		}

		fprintf(output, "Hash map contents:\n");
		fprintf(output, "--------------------------------\n");
		fprintf(output, "%-18s %-18s %5s %-64s %5s %-64s %6s %8s %14s\n", "callsite1", "callsite2",
						"line1", "exename1", "line2", "exename2", "allocs", "total sz", "avg sz");
		bool firstLine;
		for(const auto& level1_entry : memAllocCountMap) {
			firstLine = true;
			void *callsite1 = level1_entry.first;
			struct addr2line_info addrInfo1, addrInfo2;
			std::unordered_map<void*, tuple<int, long long>> level2_map = level1_entry.second;

			addrInfo1 = addr2line(callsite1);
			for(const auto& level2_entry : level2_map) {
				void *callsite2 = level2_entry.first;
				std::tuple<int, long long> tuple = level2_entry.second;
				int count = std::get<0>(tuple);
				long long size = std::get<1>(tuple);
				addrInfo2 = addr2line(callsite2);
				float avgSize = size / (float) count;
				if(firstLine)
					fprintf(output, "%-18p ", callsite1);
				else
					fprintf(output, "%-18s ", "\"");
				fprintf(output, "%-18p ", callsite2);
				fprintf(output, "%5d ", addrInfo1.lineNum);
				fprintf(output, "%-64s ", addrInfo1.exename);
				fprintf(output, "%5d ", addrInfo2.lineNum);
				fprintf(output, "%-64s ", addrInfo2.exename);
				fprintf(output, "%6d ", count);
				fprintf(output, "%8lld ", size);
				fprintf(output, "%14.1f", avgSize);
				fprintf(output, "\n");
				firstLine = false;
			}
		}
		fprintf(output, "\n\n");
		fclose(output);
	}

	struct addr2line_info addr2line(void *addr) {
		int fd[2];
		char strCallsite[16];
		char strInfo[512];
		struct addr2line_info info;

		if(pipe(fd) == -1) {
			printf("ERROR: unable to create pipe\n");
			strcpy(info.exename, "error");
			info.lineNum = 0;
			info.error = true;
			return info;
		}

		switch(fork()) {
			case -1:
				printf("ERROR: unable to fork child process\n");
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
					printf("ERROR: unable to read from pipe\n");
					strcpy(info.exename, "error");
					info.lineNum = 0;
					info.error = true;
					return info;
				}

				// Tokenize the return string, breaking apart by ':'
				// Take the second token, which will be the line number.
				char *token = strtok(strInfo, ":");
				strncpy(info.exename, token, 256);
				token = strtok(NULL, ":");
				info.lineNum = atoi(token);
		}

		return info;
	}

  void xxfree(void* ptr) {
		if(ptr >= (void *)tmpbuff && ptr <= (void *)(tmpbuff + tmppos))
			fprintf(stderr, "info: freeing temp memory\n");
		else
			Real::free(ptr);
  }

	void * xxcalloc(size_t nelem, size_t elsize) {
		if(!Real::initialized) {
			void *ptr = malloc(nelem*elsize);
			if(ptr)
				memset(ptr, 0, nelem*elsize);
			return ptr;
		}

		void *ptr = Real::calloc(nelem, elsize);
		return ptr;
	}

	void * xxrealloc(void * ptr, size_t sz) {
		//return Real::realloc(ptr, sz);
		return NULL;
	}

	size_t xxmalloc_usable_size(void *ptr) {
		//return Real::malloc_usable_size(ptr);
		return 0;
	}
}

// Thread functions
extern "C" {
  int pthread_create(pthread_t* tid, const pthread_attr_t* attr, void* (*start_routine)(void*),
		     void* arg) {
    return xthread::getInstance().thread_create(tid, attr, start_routine, arg);
  }
}
