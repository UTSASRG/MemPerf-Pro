#include <dlfcn.h>
#include <stdlib.h>
#include "real.hh"

#define DEFINE_WRAPPER(name) decltype(::name) * name;
#define INIT_WRAPPER(name, handle) name = (decltype(::name)*)dlsym(handle, #name);

namespace Real {
	DEFINE_WRAPPER(free);
	DEFINE_WRAPPER(malloc);
	DEFINE_WRAPPER(pthread_create);
	DEFINE_WRAPPER(pthread_join);
	DEFINE_WRAPPER(pthread_kill);

	void initializer() {
		INIT_WRAPPER(free, RTLD_NEXT);
		INIT_WRAPPER(malloc, RTLD_NEXT);
		
		void *pthread_handle = dlopen("libpthread.so.0", RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
		INIT_WRAPPER(pthread_create, pthread_handle);
		INIT_WRAPPER(pthread_join, pthread_handle);
		INIT_WRAPPER(pthread_kill, pthread_handle);
	}
}
