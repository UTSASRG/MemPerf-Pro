#ifndef MMPROF_XTHREADX_HH
#define MMPROF_XTHREADX_HH
#include <sys/syscall.h>
#include <stdio.h>

#include "memsample.h"
#include "real.hh"
#include "objTable.h"
#include "mymalloc.h"
#include "threadlocalstatus.h"
#include "globalstatus.h"
#include "predictor.h"
//
//typedef struct thread {
//        pthread_t * thread;
//        pid_t tid;
//        threadFunction * startRoutine;
//        void * startArg;
//        void * result;
//    } thread_t;

class xthreadx {
	typedef void * threadFunction(void *);
public:
//    typedef struct thread {
//        pthread_t * thread;
//        pid_t tid;
//        threadFunction * startRoutine;
//        void * startArg;
//        void * result;
//    } thread_t;
	static int thread_create(pthread_t * tid, const pthread_attr_t * attr, threadFunction * fn, void * arg);
	static void * startThread(void * arg);
	static void threadExit();
    static int thread_join(pthread_t thread, void ** retval);
};

#endif