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
thread_local std::unordered_map<void*, std::tuple<int, int>> u;

__attribute__((constructor)) void initializer() {
	Real::initializer();	
}

__attribute__((destructor)) void finalizer() {
  xmemory::getInstance().finalize();
}

typedef int (*main_fn_t)(int, char**, char**);
main_fn_t real_main;

// Doubletake's main function
int doubletake_main(int argc, char** argv, char** envp) {
  xmemory::getInstance().initialize();
	
	initialized = true;

	initSampling();

  // Call the program's main function
  return real_main(argc, argv, envp);
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

  void* xxmalloc(size_t sz) {
    void* ptr = NULL;

		printf("inside xxmalloc(%ld), initialized=%d \n", sz, initialized);
		if(initialized && !insideHashMap) {
			insideHashMap = true;
			void *callsite = __builtin_extract_return_addr(__builtin_return_address(1));

			std::unordered_map<void*, std::tuple<int, int>>::iterator search = u.find(callsite);
			if(search != u.end()) {
				std::tuple<int, int> found = search->second;
				int oldCount = std::get<0>(found);
				int oldSize = std::get<1>(found);
				//printf("found existing callsite: %p! old count =  %d; old allocation size = %d; old map size = %lu\n", callsite, oldCount, oldSize, u.size());
				search->second = std::make_tuple((oldCount+1), (oldSize+sz));
			} else {
				//printf("adding new callsite: %p! old map size = %lu\n", callsite, u.size());
				std::tuple<int, int> newTuple(std::make_tuple(1, sz));
				u.insert({callsite, newTuple});
			}
			printf("Hash map updated; new contents:\n");
			for(const auto& n : u) {
				std::tuple<int, int> tuple = n.second;
				int count = std::get<0>(tuple);
				int size = std::get<1>(tuple);
				printf("Key:[%p] Value:[(%d, %d)]\n", n.first, count, size);
			}
			printf("\n");
		}
		insideHashMap = false;

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

