/**
 * @file libtestperf.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 * @author Sam Silvestro <sam.silvestro@utsa.edu>
 */

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"

#include <dlfcn.h>
#include <malloc.h>

#include "libtestperf.h"
#include "real.hh"
#include "xthread.hh"

__thread allocData memRegion[3];

FILE * output = NULL;

extern char * program_invocation_name;

typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main;

bool const debug = false;
bool mallocInitialized = false;
char * tmpbuf;
unsigned int numTempAllocs = 0;
unsigned int tmppos = 0;

extern "C" {
	// Function aliases
	void free(void *) __attribute__ ((weak, alias("xxfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("xxcalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("xxmalloc")));
	void * realloc(void *, size_t) __attribute__ ((weak, alias("xxrealloc")));
}

__attribute__((constructor)) void initializer() {
	// Allocate memory for our pre-init temporary allocator
	if((tmpbuf = (char *)mmap(NULL, TEMP_BUF_SIZE, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
        perror("error: unable to allocate temporary memory");
		abort();
    }

	// If debug mode is enabled, open an output file for write access.
	if(debug) {
		// Generate the name of our output file, then open it for writing.
		char outputFile[MAX_FILENAME_LEN];
		pid_t pid = getpid();
		snprintf(outputFile, MAX_FILENAME_LEN,
				"%s_libtestperf_pid_%d_tid_%d.txt",
				program_invocation_name, pid, pid);
		// Will overwrite current file; change the fopen flag to "a" for append.
		output = fopen(outputFile, "w");
		if(output == NULL) {
			fprintf(stderr, "error: unable to open output file for write\n");
			return;
		}
	}

	int i;
	for(i = 0; i < 3; i++) {
		// Allocate memory region for our private allocator
		if((memRegion[i].privAllocBegin = mmap(NULL, PRIV_ALLOC_SIZE,
						PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
						-1, 0)) == MAP_FAILED) {
			perror("error: unable to allocate memory for private allocator");
			abort();
		}
		// Initialize the head pointer of
		// each region equal to their starting address.
		memRegion[i].privAllocHead = memRegion[i].privAllocBegin;

		// Calculate and record the last usable address of each memory region
		// (will be used for bounds-checking during runtime).
		memRegion[i].privAllocEnd =
			(void *)((char *)memRegion[i].privAllocBegin + PRIV_ALLOC_SIZE - 1);

		// Calculate and record the high-order four bytes of each memory
		// region's addresses.
		memRegion[i].highWord =
			(uint64_t)memRegion[i].privAllocBegin & 0xFFFFFFFF00000000;

		if(debug) {
			fprintf(output, ">> private allocator memory (%d byte) @ %p ~ %p "
					"(size %ld)\n", ((i + 1) * 4), memRegion[i].privAllocBegin,
					memRegion[i].privAllocEnd, PRIV_ALLOC_SIZE);
			fprintf(output, ">> computed allocator high word (%d byte) = 0x%lx\n",
					((i + 1) * 4), memRegion[i].highWord);
			fflush(output);
		}
	}

	Real::initializer();
	mallocInitialized = true;
}

__attribute__((destructor)) void finalizer() {
	if(debug) {
		fclose(output);
	}
}

// MemPerf's main function
int libtestperf_main(int argc, char ** argv, char ** envp) {
	return real_main(argc, argv, envp);
}

extern "C" int __libc_start_main(main_fn_t, int, char **, void (*)(), void (*)(), void (*)(), void *) __attribute__((weak, alias("libtestperf_libc_start_main")));

extern "C" int libtestperf_libc_start_main(main_fn_t main_fn, int argc, char ** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(), void * stack_end) {
	auto real_libc_start_main = (decltype(__libc_start_main) *)dlsym(RTLD_NEXT, "__libc_start_main");
	real_main = main_fn;
	return real_libc_start_main(libtestperf_main, argc, argv, init, fini, rtld_fini, stack_end);
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

		// Fulfill requests for small objects whose size is a multiple of 4
		// using our private allocator's memory region.
		if((sz % 4 == 0) && ((sz > 0) && (sz <= 12))) {
			int numUnits = sz / 4;
			int index = numUnits - 1;

			uint32_t * privAllocHeadTyped =
				(uint32_t *)memRegion[index].privAllocHead;
			if(debug) {
				fprintf(output, "malloc request: sz = %zu, privAllocHeadTyped ="
						" %p, privAllocBegin = %p, privAllocEnd = %p\n", sz,
						privAllocHeadTyped, memRegion[index].privAllocBegin,
						memRegion[index].privAllocEnd);
				fflush(output);
			}
			uint32_t headValue = *privAllocHeadTyped;
			void * oldHead = memRegion[index].privAllocHead;
			if(headValue) {
				uint32_t nextFreeAddrLowWord = headValue;
				uint64_t nextFreeAddr =
					nextFreeAddrLowWord | memRegion[index].highWord;
				void * nextFree = (void *)nextFreeAddr;
				memRegion[index].privAllocHead = nextFree;
				if(debug) {
					fprintf(output, ">> head element has value: 0x%x\n",
							headValue);
					fprintf(output, ">> nextFreeAddrLowWord = 0x%x, highWord = "
							"0x%lx\n", nextFreeAddrLowWord,
							memRegion[index].highWord);
					fprintf(output, ">> changing head element's value to: %p\n",
							nextFree);
					fflush(output);
				}
			} else {
				if(debug) {
					fprintf(output, ">> head element has no value, "
							"incrementing head\n");
					fflush(output);
				}
				privAllocHeadTyped += numUnits;
				memRegion[index].privAllocHead = (void *)privAllocHeadTyped;
				if(memRegion[index].privAllocHead >
						memRegion[index].privAllocEnd) {
					if(debug)
						fprintf(output, "ERROR: private allocator exhausted!\n");
					fprintf(stderr, "ERROR: private allocator exhausted!\n");
					abort();
				}
			}
			return oldHead;
		} else {
			return Real::malloc(sz);
		}
	}

	void * xxcalloc(size_t nelem, size_t elsize) {
		void * ptr = NULL;
		if((ptr = malloc(nelem * elsize)))
			memset(ptr, 0, nelem * elsize);
		return ptr;
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
		} else if(((ptr >= memRegion[0].privAllocBegin) && (ptr <= memRegion[0].privAllocEnd)) ||
				((ptr >= memRegion[1].privAllocBegin) && (ptr <= memRegion[1].privAllocEnd)) ||
				((ptr >= memRegion[2].privAllocBegin) && (ptr <= memRegion[2].privAllocEnd))) {

			int index;
			if((ptr >= memRegion[0].privAllocBegin) && (ptr <= memRegion[0].privAllocEnd)) {
				index = 0;
			} else if((ptr >= memRegion[1].privAllocBegin) && (ptr <= memRegion[1].privAllocEnd)) {
				index = 1;
			} else if((ptr >= memRegion[2].privAllocBegin) && (ptr <= memRegion[2].privAllocEnd)) {
				index = 2;
			} else {
				fprintf(stderr, "ERROR: unable to identify to which group "
						"ptr=%p belongs\n", ptr);
				fflush(stderr);
				abort();
			}
			void * oldHead = memRegion[index].privAllocHead;
			memRegion[index].privAllocHead = ptr;
			uint32_t * privLoc = (uint32_t *)ptr;
			uint32_t lowWord = (uint64_t)oldHead & 0xFFFFFFFF;
			if(debug) {
				fprintf(output, ">> freeing private allocator memory @ %p\n",
						ptr);
				fprintf(output, ">> old head = %p, new head = %p\n",
						oldHead, ptr);
				fprintf(output, ">> writing half-pointer to new head @ %p of "
						"0x%x\n", privLoc, lowWord);
				fflush(output);
			}
			*privLoc = lowWord;
		} else {
			Real::free(ptr);
		}
	}

	void * xxrealloc(void * ptr, size_t sz) {
		// When ptr is null, realloc is effectively
		// equivalent to a malloc request.
		if(ptr == NULL)
			return xxmalloc(sz);

		// If malloc is not yet initialized (due to a dlsym call not having
		// returned yet) then we treat this realloc request as separate free
		// and malloc requests passed to our private temporary allocator.
		if(!mallocInitialized) {
			xxfree(ptr);
			return xxmalloc(sz);
		}

		// Determine whether the target of this realloc request is contained
		// within our private 4/8/12 byte allocator memory. If so, we will
		// simply treat the request equivalently as separate free and malloc
		// requests.
		int i;
		for(i = 0; i < 3; i++) {
			if((ptr >= memRegion[i].privAllocBegin) && (ptr <= memRegion[i].privAllocEnd)) {
				xxfree(ptr);
				return xxmalloc(sz);
			}
		}

		// Otherwise, if none of the above conditions apply, we can pass the
		// realloc request on to glibc's realloc routine.
		return Real::realloc(ptr, sz);
	}

	// Intercept thread creation
	int pthread_create(pthread_t * tid, const pthread_attr_t * attr, void * (*start_routine)(void *),
			void * arg) {
		return xthread::thread_create(tid, attr, start_routine, arg);
	}
}
