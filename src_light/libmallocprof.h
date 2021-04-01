#ifndef __LIBMALLOCPROF_H__
#define __LIBMALLOCPROF_H__

#include <limits.h>
#include <dlfcn.h> //dlsym
#include <fcntl.h> //fopen flags
#include <stdio.h> //print, getline
#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include <new>
#include <sched.h>
#include <stdlib.h>
#include "programstatus.h"
#include "mymalloc.h"
#include "allocatingstatus.h"
#include "threadlocalstatus.h"
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "real.hh"
#include "spinlock.hh"
#include "xthreadx.hh"
#include "objTable.h"
#include "globalstatus.h"
#include "memsample.h"
#include "shadowmemory.hh"
#include "definevalues.h"
#include "libmallocprof.h"

#endif /* end of include guard: __LIBMALLOCPROF_H__ */
