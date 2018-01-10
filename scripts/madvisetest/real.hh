#ifndef __REAL_HH_
#define __REAL_HH_

#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#define DECLARE_WRAPPER(name) extern decltype(::name) * name;

namespace Real {
	void initializer();
	DECLARE_WRAPPER(free);
	DECLARE_WRAPPER(malloc);
  DECLARE_WRAPPER(pthread_create);
  DECLARE_WRAPPER(pthread_join);
  DECLARE_WRAPPER(pthread_kill);
};

#endif
