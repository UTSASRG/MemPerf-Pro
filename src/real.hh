#ifndef DOUBLETAKE_REAL_H
#define DOUBLETAKE_REAL_H

#include <malloc.h>
#include <pthread.h>

#define DECLARE_WRAPPER(name) extern decltype(::name) * name;

namespace Real {
	void initializer();
	DECLARE_WRAPPER(free);
	DECLARE_WRAPPER(calloc);
	DECLARE_WRAPPER(malloc);
	//DECLARE_WRAPPER(malloc_usable_size);
	//DECLARE_WRAPPER(realloc);
	DECLARE_WRAPPER(pthread_create);
};

#endif
