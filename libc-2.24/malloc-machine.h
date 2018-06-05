/* Basic platform-independent macro definitions for mutexes,
   thread-specific data and parameters for malloc.
   Copyright (C) 2003-2016 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef _GENERIC_MALLOC_MACHINE_H
#define _GENERIC_MALLOC_MACHINE_H

#include <time/time.h>
#include <xlocale.h>
/*
//# ifdef __USE_GNU
typedef struct __locale_struct
{
  const unsigned short int *__ctype_b;
  const int *__ctype_tolower;
  const int *__ctype_toupper;

  const char *__names[13];
} *__locale_t;
*/

/* POSIX 2008 makes locale_t official.  */
typedef __locale_t locale_t;
extern char *strptime_l (const char *__restrict __s,
       const char *__restrict __fmt, struct tm *__tp,
       __locale_t __loc) __THROW;
//# endif
extern __typeof (strptime_l) __strptime_l;
//#ifdef _LIBC
//weak_alias (__strptime_l, strptime_l)
//#endif

struct timespec
  {
    __time_t tv_sec;    /* Seconds.  */
    __syscall_slong_t tv_nsec;  /* Nanoseconds.  */
  };

#define __USE_XOPEN2K
#define __USE_MISC
#include <bits-sigset.h>
#include <signal/signal.h>
#include <setjmp/setjmp.h>
#include <time.h>
#include <bin-time.h>
#include <string.h>
#include <sys/types.h>
#include <bits/local_lim.h>

#include <strings.h>
#include "include/libc-symbols.h"
#include <bits-pthreadtypes.h>
#include <atomic.h>

#ifndef atomic_full_barrier
# define atomic_full_barrier() __asm ("" ::: "memory")
#endif

#ifndef atomic_read_barrier
# define atomic_read_barrier() atomic_full_barrier ()
#endif

#ifndef atomic_write_barrier
# define atomic_write_barrier() atomic_full_barrier ()
#endif

#ifndef DEFAULT_TOP_PAD
# define DEFAULT_TOP_PAD 131072
#endif

#endif /* !defined(_GENERIC_MALLOC_MACHINE_H) */
