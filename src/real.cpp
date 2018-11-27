#include <dlfcn.h>
#include <unistd.h>
#include "real.hh"

#define DEFINE_WRAPPER(name) decltype(::name) * name;
#define INIT_WRAPPER(name, handle) name = (decltype(::name)*)dlsym(handle, #name);

extern bool realInitialized;

namespace RealX {
	DEFINE_WRAPPER(brk);
	DEFINE_WRAPPER(sbrk);
	DEFINE_WRAPPER(free);
	DEFINE_WRAPPER(calloc);
	DEFINE_WRAPPER(malloc);
	DEFINE_WRAPPER(realloc);
	DEFINE_WRAPPER(memalign);
	DEFINE_WRAPPER(posix_memalign);
	DEFINE_WRAPPER(pthread_create);
	DEFINE_WRAPPER(pthread_join);
	DEFINE_WRAPPER(mmap);
	DEFINE_WRAPPER(munmap);
	DEFINE_WRAPPER(mremap);
	DEFINE_WRAPPER(pthread_mutex_lock);
	DEFINE_WRAPPER(pthread_mutex_unlock);
	DEFINE_WRAPPER(pthread_mutex_trylock);
  DEFINE_WRAPPER(madvise);
  DEFINE_WRAPPER(mprotect);

	void initializer() {

		if (realInitialized) return;

		INIT_WRAPPER(brk, RTLD_NEXT);
		INIT_WRAPPER(sbrk, RTLD_NEXT);
		INIT_WRAPPER(free, RTLD_NEXT);
		INIT_WRAPPER(calloc, RTLD_NEXT);
		INIT_WRAPPER(malloc, RTLD_NEXT);
		INIT_WRAPPER(realloc, RTLD_NEXT);
		INIT_WRAPPER(memalign, RTLD_NEXT);
		INIT_WRAPPER(posix_memalign, RTLD_NEXT);
		INIT_WRAPPER(mmap, RTLD_NEXT);
		INIT_WRAPPER(munmap, RTLD_NEXT);
		INIT_WRAPPER(mremap, RTLD_NEXT);
    INIT_WRAPPER(madvise, RTLD_NEXT);
    INIT_WRAPPER(mprotect, RTLD_NEXT);
		INIT_WRAPPER(pthread_create, RTLD_NEXT);
		INIT_WRAPPER(pthread_join, RTLD_NEXT);
		INIT_WRAPPER(pthread_mutex_lock, RTLD_NEXT);
		INIT_WRAPPER(pthread_mutex_unlock, RTLD_NEXT);
		INIT_WRAPPER(pthread_mutex_trylock, RTLD_NEXT);

		realInitialized = true;
	}
}
