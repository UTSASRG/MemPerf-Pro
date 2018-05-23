#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#include "real.hh"

#define DEFINE_WRAPPER(name) decltype(::name) * name;
#define INIT_WRAPPER(name, handle) name = (decltype(::name)*)dlsym(handle, #name);

namespace Real {
	DEFINE_WRAPPER(brk);
	DEFINE_WRAPPER(sbrk);
	DEFINE_WRAPPER(free);
	DEFINE_WRAPPER(calloc);
	DEFINE_WRAPPER(malloc);
	DEFINE_WRAPPER(realloc);
	DEFINE_WRAPPER(pthread_create);
	DEFINE_WRAPPER(mmap);

	void initializer() {
		INIT_WRAPPER(brk, RTLD_NEXT);
		INIT_WRAPPER(sbrk, RTLD_NEXT);
		INIT_WRAPPER(free, RTLD_NEXT);
		INIT_WRAPPER(calloc, RTLD_NEXT);
		INIT_WRAPPER(malloc, RTLD_NEXT);
		INIT_WRAPPER(realloc, RTLD_NEXT);
		INIT_WRAPPER(mmap, RTLD_NEXT);

		void *pthread_handle = dlopen("libpthread.so.0", RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
		INIT_WRAPPER(pthread_create, pthread_handle);
	}
}
