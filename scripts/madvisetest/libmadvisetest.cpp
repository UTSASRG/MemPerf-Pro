#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <unistd.h>
#include <string.h>
#include "real.hh"
#include "xthread.hh"
#include "mm.hh"
#include <libsyscall_intercept_hook_point.h>


#ifdef RDTSC
#warning using rdtsc instead of rdtscp
#define READ_TSC rdtsc
#else
#define READ_TSC rdtscp
#endif
#define SEPERATOR "------------------------------------------------------\n"
#define CPU_CLOCK_FREQ 2000000000

int xxintercept_hook_point(long syscall_number,
        long arg0, long arg1,
        long arg2, long arg3,
        long arg4, long arg5,
        long *result);

void print_madvise_data();
unsigned long long start_time;
madv_t _madvise_data[MAX_ALIVE_THREADS];
intptr_t globalStackAddr;

typedef int (*main_fn_t)(int, char**, char**);

extern "C" int __libc_start_main(main_fn_t, int, char**, void (*)(), void (*)(), void (*)(), void*) __attribute__((weak, alias("madvisetest_libc_start_main")));

extern "C" int madvisetest_libc_start_main(main_fn_t main_fn, int argc, char** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
		intercept_hook_point = xxintercept_hook_point;

		memset(&_madvise_data, 0, sizeof(_madvise_data));
    for(int i = 0; i < MAX_ALIVE_THREADS; i++) {
      _madvise_data[i].tindex = i;
		}

		// allocate stack area
		size_t stackSize = (size_t)STACK_SIZE * MAX_THREADS;
		if((globalStackAddr = (intptr_t)MM::mmapAllocatePrivate(stackSize)) == 0) {
				FATAL("Failed to initialize stack area\n");
		}
		syscall_no_intercept(SYS_madvise, (long)globalStackAddr, (long)stackSize, (long)MADV_NOHUGEPAGE);
		//madvise((void *)globalStackAddr, stackSize, MADV_NOHUGEPAGE);

		#warning turned off use of guard pages between thread stacks
		/*
		// set guard pages in cusotmized stack area. Set in both the beginnning and end.
		// better way is to set this when we use a new thread index, which may require changing the bool flag in thread_t to a int.
		for(intptr_t i = 1; i < MAX_THREADS; i++) { // ingore the first thread
		intptr_t stackStart = globalStackAddr + i * STACK_SIZE;
		if(0 != mprotect((void*)(stackStart + STACK_SIZE - GUARD_PAGE_SIZE), GUARD_PAGE_SIZE, PROT_NONE)
		|| 0 != mprotect((void*)stackStart, GUARD_PAGE_SIZE, PROT_NONE)) {
		fprintf(stderr, "Failed to set guard pages\n");
		abort();
		}
		}
		 */

		#ifdef CUSTOMIZED_MAIN_STACK
		intptr_t ebp, esp, customizedEbp, customizedEsp, ebpOffset, espOffset;
		intptr_t stackTop = (((intptr_t)&main_fn + PageSize) & ~(PageSize - 1)) + PageSize; // page align
		intptr_t newStackTop = globalStackAddr + STACK_SIZE - GUARD_PAGE_SIZE;
		// get current stack
		#if defined(X86_32BIT)
		asm volatile("movl %%ebp,%0\n"
						"movl %%esp,%1\n"
						: "=r"(ebp), "=r"(esp)::"memory");
		#else
		asm volatile("movq %%rbp,%0\n"
						"movq %%rsp, %1\n"
						: "=r"(ebp), "=r"(esp)::"memory");
		#endif
		// copy stack data
		ebpOffset = stackTop - ebp;
		espOffset = stackTop - esp;
		customizedEbp = newStackTop - ebpOffset;
		customizedEsp = newStackTop - espOffset;
		memcpy((void*)customizedEsp, (void*)esp, espOffset);
		#if defined(X86_32BIT)
		asm volatile("movl %0, %%ebp\n"
						"movl %1, %%esp\n"
						:: "r"(customizedEbp), "r"(customizedEsp):"memory");
		#else
		asm volatile("movq %0,%%rbp\n"
						"movq %1,%%rsp\n"
						:: "r"(customizedEbp), "r"(customizedEsp):"memory");
		#endif
		// re-direct arguments
		argv = (char**)(newStackTop - (stackTop - (intptr_t)argv));

		for(int i = 0; i < argc; i++) {
				argv[i] = (char*)(newStackTop - (stackTop - (intptr_t)argv[i]));
		}

		stack_end = (void*)(newStackTop - (stackTop - (intptr_t)stack_end));
		// re-direct arguments
		// reset original stack

		memset((void*)esp, 0, espOffset);
		#endif

		// real run
		atexit(print_madvise_data);

		start_time = rdtscp();
		auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");
		return real_libc_start_main(main_fn, argc, argv, init, fini, rtld_fini, stack_end);
}

// Variables used by our pre-init private allocator
typedef enum {
		E_HEAP_INIT_NOT = 0,
		E_HEAP_INIT_WORKING,
		E_HEAP_INIT_DONE,
} eHeapInitStatus;

eHeapInitStatus heapInitStatus = E_HEAP_INIT_NOT;

__attribute__((constructor)) void heapinitialize() {
		intercept_hook_point = xxintercept_hook_point;

		if(heapInitStatus == E_HEAP_INIT_NOT) {
				heapInitStatus = E_HEAP_INIT_WORKING;
				// The following function will invoke dlopen and will call malloc in the end.
				// Thus, it is putted in the end so that it won't fail
				Real::initializer();
				xthread::getInstance().initialize();
				heapInitStatus = E_HEAP_INIT_DONE;
		} else {
				while(heapInitStatus != E_HEAP_INIT_DONE);
		}
}

// Intercept thread creation
int pthread_create(pthread_t * tid, const pthread_attr_t * attr,
				void *(*start_routine)(void *), void * arg) {
		if(heapInitStatus != E_HEAP_INIT_DONE) {
				heapinitialize();
		}
		return xthread::getInstance().thread_create(tid, attr, start_routine, arg);
}
int pthread_join(pthread_t tid, void** retval) {
		return xthread::getInstance().thread_join(tid, retval);
}

int xxintercept_hook_point(long syscall_number,
				long arg0, long arg1,
				long arg2, long arg3,
				long arg4, long arg5,
				long *result) {
		if (syscall_number == SYS_madvise) {
				// Prevent the application from using this syscall. From the point of
				// view of the calling process, it is as if the kernel would return
				// the ENOTSUP error code from the syscall.
				//*result = -ENOTSUP;
				//return 0;

				unsigned long long time1 = READ_TSC();
				syscall_no_intercept(syscall_number, arg0, arg1, arg2, arg3, arg4, arg5, result);
				unsigned long long time2 = READ_TSC();

				/*
				char buffer[128];
				snprintf(buffer, 128, "arg1=%ld\n", arg1);
				*result = syscall_no_intercept(SYS_write, 1, buffer, strlen(buffer));
				*/

				int tindex = getThreadIndex(&tindex);
				madv_t * madv_data = &_madvise_data[tindex];
				madv_data->numCalls++;
				madv_data->timeSum += (time2 - time1);
				madv_data->sizeSum += arg1;

				return 0;
		/*
		} else if(syscall_number == SYS_brk) {
				char buffer[128];
				snprintf(buffer, 128, "brk(arg0=0x%lx, arg1=0x%lx, arg2=0x%lx, arg3=0x%lx, arg4=0x%lx, arg5=0x%lx)\n",
								arg0, arg1, arg2, arg3, arg4, arg5);
				*result = syscall_no_intercept(SYS_write, 1, buffer, strlen(buffer));
				return 1;
		*/
		} else {
				// Ignore any other syscall (pass them on to the kernel as would normally happen.
				return 1;
		}
}

void print_madvise_data() {
		for(int i = 0; i < MAX_ALIVE_THREADS; i++) {
				madv_t * data = &_madvise_data[i];
				if(data->numCalls > 0) {
						PRINT("thread %2d: %ld madvise calls, %ld total time, %zu total size",
										i, data->numCalls, data->timeSum, data->sizeSum);
				}
		}
}
