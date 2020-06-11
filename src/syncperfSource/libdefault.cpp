//#include <stdio.h>
//#include <stdlib.h>
//#include <dlfcn.h>
//#include <stdarg.h>
//#include <assert.h>
//#include "xrun.h"
//#include "mutex_manager.h"
//#include "recordentries.hh"
//#include "report.h"
//#include "mutex.h"
//
//void checkDataStructures() {
//    assert(__SIZEOF_PTHREAD_MUTEX_T == 40);
//    assert(__SIZEOF_PTHREAD_ATTR_T == 56);
//    assert(__SIZEOF_PTHREAD_RWLOCK_T == 56);
//    assert(__SIZEOF_PTHREAD_BARRIER_T == 32);
//    assert(__SIZEOF_PTHREAD_MUTEXATTR_T == 4);
//    assert(__SIZEOF_PTHREAD_COND_T == 48);
//    assert(__SIZEOF_PTHREAD_CONDATTR_T == 4);
//    assert(__SIZEOF_PTHREAD_RWLOCKATTR_T == 8);
//    assert(__SIZEOF_PTHREAD_BARRIERATTR_T == 4);
//}


//extern "C" {
//  void initializer (void) __attribute__((constructor));
//  void finalizer (void)   __attribute__((destructor));
//  bool initialized = false;
//
//
//  __thread thread_t * current = NULL;
//  bool _isMultithreading = false;
//
//	RecordEntries<mutex_t>sync_vars;
//
//	void *thread_sync_data;//TODO: for storing thread local mutex data
//
//  void initializer (void) {
//    // Using globals to provide allocation
//    // before initialized.
//
//    init_real_functions();
//
//    xrun::getInstance().initialize();
//		sync_vars.initialize(xdefines::MAX_SYNC_ENTRIES);
//		thread_sync_data = MM::mmapAllocatePrivate(xdefines::MAX_SYNC_ENTRIES * xdefines::MAX_THREADS * sizeof(thread_mutex_t));
//
//
//      checkDataStructures();
//
//    initialized = true;
//
//		//fprintf(stderr, "Now we have initialized successfully\n");
//
//  }
//
//  void finalizer (void) {
//    initialized = false;
//    xrun::getInstance().finalize();
//		Report::getInstance().print(sync_vars);
//  }
//
//
//
//	 Intercept the pthread_create function.
//  int pthread_create (pthread_t * tid, const pthread_attr_t * attr, void *(*start_routine) (void *), void * arg)
//  {
////		 fprintf(stderr, "In my thread_create\n");
//     return xthread::getInstance().thread_create(tid, attr, start_routine, arg);
//  }
//
//	// Intercept the pthread_join function. Thus,
//	// we are able to know that how many threads have exited.
//	int pthread_join(pthread_t thread, void **retval) {
////      fprintf(stderr, "In my thread_join\n");
//     return xthread::getInstance().thread_join(thread, retval);
//	}
//
//};

