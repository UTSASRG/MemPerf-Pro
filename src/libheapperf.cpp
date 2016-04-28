/**
 * @file libheapperf.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */

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

#include "xmemory.hh"
#include "xthread.hh"
#include "memsample.h"

// Install xxmalloc, xxfree, etc. as custom allocator
#include "wrappers/gnuwrapper.cpp"

using namespace std;
enum { InitialMallocSize = 1024 * 1024 * 1024 };

bool initialized = false;
__thread bool insideHashMap = false;

__thread thread_t * current;
xpheap<xoneheap<xheap>> xmemory::_pheap;
//thread_local std::unordered_map<void*, std::tuple<int, int>> memAllocCountMap;
thread_local std::unordered_map<void*, std::unordered_map<void*, tuple<int, int>>> memAllocCountMap;

__attribute__((constructor)) void initializer() {
	Real::initializer();	
}

__attribute__((destructor)) void finalizer() {
  xmemory::getInstance().finalize();
}

typedef int (*main_fn_t)(int, char**, char**);
main_fn_t real_main;
extern "C" void printHashMap();

// Doubletake's main function
int doubletake_main(int argc, char** argv, char** envp) {
  xmemory::getInstance().initialize();
	
	initialized = true;

	initSampling();

	int main_ret_val = real_main(argc, argv, envp);
	printHashMap();

  return main_ret_val;
}

extern "C" int __libc_start_main(main_fn_t, int, char**, void (*)(), void (*)(), void (*)(), void*) __attribute__((weak, alias("doubletake_libc_start_main")));

extern "C" int doubletake_libc_start_main(main_fn_t main_fn, int argc, char** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
  // Find the real __libc_start_main
  auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");
  // Save the program's real main function
  real_main = main_fn;
  // Run the real __libc_start_main, but pass in doubletake's main function
  return real_libc_start_main(doubletake_main, argc, argv, init, fini, rtld_fini, stack_end);
}


// Temporary bump-pointer allocator for malloc() calls before DoubleTake is initialized
static void* tempmalloc(int size) {
  static char _buf[InitialMallocSize];
  static int _allocated = 0;

  if(_allocated + size > InitialMallocSize) {
    fprintf(stderr, "Not enough space for tempmalloc");
    abort();
  } else {
    void* p = (void*)&_buf[_allocated];
    _allocated += size;
    return p;
  }
}

// Memory management functions
extern "C" {
	struct stack_frame {
  	struct stack_frame * prev;/* pointing to previous stack_frame */
  	void* caller_address;/* the address of caller */
	};

	extern void *__libc_stack_end;

  void* xxmalloc(size_t sz) {
    void* ptr = NULL;

		//printf("inside xxmalloc(%ld), initialized=%d, insideHashMap=%d\n", sz, initialized, insideHashMap);

		if(initialized && !insideHashMap) {
			insideHashMap = true;

			struct stack_frame *current_frame;
			current_frame = (struct stack_frame*)(__builtin_frame_address(0));
			void *callsite1 = current_frame->caller_address;
			current_frame = current_frame->prev;
			void *callsite2 = current_frame->caller_address;

			//printf(">>> debug: callsite1, callsite2 == %p, %p\n", callsite1, callsite2);

			std::unordered_map<void*, std::unordered_map<void*, tuple<int, int>>>::iterator search_level1 =
							memAllocCountMap.find(callsite1);
	
			// If we found a match on callsite1...
			if(search_level1 != memAllocCountMap.end()) {
				//printf(">>> found key=%p in level 1 map\n", callsite1);
				std::unordered_map<void*, tuple<int, int>> found_level1 = search_level1->second;
				std::unordered_map<void*, tuple<int, int>>::iterator search_level2 = found_level1.find(callsite2);

				// If we found a match on callsite2...
				if(search_level2 != found_level1.end()) {
					//printf(">>> found key=%p in level 2 map for key=%p in level 1\n", callsite2, callsite1);
					std::tuple<int, int> found_level2 = search_level2->second;
					int oldCount = std::get<0>(found_level2);
					int oldSize = std::get<1>(found_level2);
					//search_level2->second = std::make_tuple((oldCount+1), (oldSize+sz));
					memAllocCountMap[callsite1][callsite2] = std::make_tuple((oldCount+1), (oldSize+sz));
				} else {	// If we did NOT find a match on callsite2...
					//printf(">>> did NOT find key=%p in level 2 map for key=%p in level 1\n", callsite2, callsite1);
					std::tuple<int, int> new_tuple(std::make_tuple(1, sz));
					//found_level1.insert({callsite2, new_tuple});
					memAllocCountMap[callsite1].insert({callsite2, new_tuple});
				}
			} else {	// If we did NOT find a match on callsite1...
				//printf(">>> did NOT find key=%p in level 1 map\n", callsite1);
				std::unordered_map<void*, tuple<int, int>> new_level2_map;
				std::tuple<int, int> newTuple(std::make_tuple(1, sz));
				new_level2_map.insert({callsite2, newTuple});
				memAllocCountMap.insert({callsite1, new_level2_map});
			}
			insideHashMap = false;
		}

		if(sz == 0) {
			sz = 1;
		}

    // Align the object size. FIXME for now, just use 16 byte alignment and min size.
    if(!initialized) {
    	if (sz < 16) {
      	sz = 16;
   		}
    	sz = (sz + 15) & ~15;
      ptr = tempmalloc(sz);
    } 
		else {
      ptr = xmemory::getInstance().malloc(sz);
    }
    if(ptr == NULL) {
    	fprintf(stderr, "Out of memory with initialized %d!\n", initialized);
      ::abort();
    }
		
		//fprintf(stderr, "MEMORY allocation with ptr %p to %lx\n", ptr, ((intptr_t)ptr + sz));
    return ptr;
  }

	void printHashMap() {
		printf("Hash map contents:\n");
		for(const auto& level1_entry : memAllocCountMap) {
			bool isFirstEntry = true;
			void *callsite1 = level1_entry.first;
			std::unordered_map<void*, tuple<int, int>> level2_map = level1_entry.second;
			for(const auto& level2_entry : level2_map) {
				void *callsite2 = level2_entry.first;
				std::tuple<int, int> tuple = level2_entry.second;
				int count = std::get<0>(tuple);
				int size = std::get<1>(tuple);
				if(isFirstEntry)
					printf("   (%-18p, (%p, (%d, %d)))\n", callsite1, callsite2, count, size);
				else
					printf("   (                  , (%p, (%d, %d)))\n", callsite2, count, size);
				isFirstEntry = false;
			}
		}
	}

  void xxfree(void* ptr) {
    if(initialized && ptr) {
      xmemory::getInstance().free(ptr);
    }
  }

  size_t xxmalloc_usable_size(void* ptr) {
    if(initialized) {
      return xmemory::getInstance().getSize(ptr);
    }
    return 0;
  }

	void * xxrealloc(void * ptr, size_t sz) {
    if(initialized) {
      return xmemory::getInstance().realloc(ptr, sz);
		}
		else {
			void * newptr = tempmalloc(sz);
			return newptr;
		}
	}
}

// Thread functions
extern "C" {
  int pthread_create(pthread_t* tid, const pthread_attr_t* attr, void* (*start_routine)(void*),
		     void* arg) {
    return xthread::getInstance().thread_create(tid, attr, start_routine, arg);
  }
}

