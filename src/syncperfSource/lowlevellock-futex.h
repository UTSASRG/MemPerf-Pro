/* Low-level locking access to futex facilities.  Linux version.
   Copyright (C) 2005-2015 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library.  If not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef _LOWLEVELLOCK_FUTEX_H
#define _LOWLEVELLOCK_FUTEX_H	1

#ifndef __ASSEMBLER__
#include <sysdep.h>
#include <tls.h>
//#include <kernel-features.h>
#endif

#define FUTEX_WAIT		0
#define FUTEX_WAKE		1
#define FUTEX_REQUEUE		3
#define FUTEX_CMP_REQUEUE	4
#define FUTEX_WAKE_OP		5
#define FUTEX_OP_CLEAR_WAKE_IF_GT_ONE	((4 << 24) | 1)
#define FUTEX_LOCK_PI		6
#define FUTEX_UNLOCK_PI		7
#define FUTEX_TRYLOCK_PI	8
#define FUTEX_WAIT_BITSET	9
#define FUTEX_WAKE_BITSET	10
#define FUTEX_WAIT_REQUEUE_PI   11
#define FUTEX_CMP_REQUEUE_PI    12
#define FUTEX_PRIVATE_FLAG	128
#define FUTEX_CLOCK_REALTIME	256

#define FUTEX_BITSET_MATCH_ANY	0xffffffff

/* Values for 'private' parameter of locking macros.  Yes, the
   definition seems to be backwards.  But it is not.  The bit will be
   reversed before passing to the system call.  */
#define LLL_PRIVATE	0
#define LLL_SHARED	FUTEX_PRIVATE_FLAG

#ifndef __ASSEMBLER__

//#if IS_IN (libc) || IS_IN (rtld)
/* In libc.so or ld.so all futexes are private.  */
//# ifdef __ASSUME_PRIVATE_FUTEX
//#  define __lll_private_flag(fl, private) \
  ((fl) | FUTEX_PRIVATE_FLAG)
//# else
//#  define __lll_private_flag(fl, private) \
  ((fl) | THREAD_GETMEM (THREAD_SELF, header.private_futex))
//# endif
//#else
# ifdef __ASSUME_PRIVATE_FUTEX
#  define __lll_private_flag(fl, my_private) \
  (((fl) | FUTEX_PRIVATE_FLAG) ^ (my_private))
# else
#  define __lll_private_flag(fl, my_private) \
  (__builtin_constant_p (my_private)					      \
   ? ((my_private) == 0							      \
      ? ((fl) | THREAD_GETMEM (THREAD_SELF, header.private_futex))	      \
      : (fl))								      \
   : ((fl) | (((my_private) ^ FUTEX_PRIVATE_FLAG)				      \
	      & THREAD_GETMEM (THREAD_SELF, header.private_futex))))
# endif
//#endif

#define lll_futex_syscall(nargs, futexp, op, ...)                       \
  ({                                                                    \
    INTERNAL_SYSCALL_DECL (__err);                                      \
    long int __ret = INTERNAL_SYSCALL (futex, __err, nargs, futexp, op, \
				       __VA_ARGS__);                    \
    (__glibc_unlikely (INTERNAL_SYSCALL_ERROR_P (__ret, __err))         \
     ? -INTERNAL_SYSCALL_ERRNO (__ret, __err) : 0);                     \
  })

#define lll_futex_wait(futexp, val, my_private) \
  lll_futex_timed_wait (futexp, val, NULL, my_private)

#define lll_futex_timed_wait(futexp, val, timeout, my_private)     \
  lll_futex_syscall (4, futexp,                                 \
		     __lll_private_flag (FUTEX_WAIT, my_private),  \
		     val, timeout)

#define lll_futex_timed_wait_bitset(futexp, val, timeout, clockbit, my_private) \
  lll_futex_syscall (6, futexp,                                         \
		     __lll_private_flag (FUTEX_WAIT_BITSET | (clockbit), \
					 my_private),                      \
		     val, timeout, NULL /* Unused.  */,                 \
		     FUTEX_BITSET_MATCH_ANY)

#define lll_futex_wake(futexp, nr, my_private)                             \
  lll_futex_syscall (4, futexp,                                         \
		     __lll_private_flag (FUTEX_WAKE, my_private), nr, 0)

/* Returns non-zero if error happened, zero if success.  */
#define lll_futex_requeue(futexp, nr_wake, nr_move, mutex, val, my_private) \
  lll_futex_syscall (6, futexp,                                         \
		     __lll_private_flag (FUTEX_CMP_REQUEUE, my_private),   \
		     nr_wake, nr_move, mutex, val)

/* Returns non-zero if error happened, zero if success.  */
#define lll_futex_wake_unlock(futexp, nr_wake, nr_wake2, futexp2, my_private) \
  lll_futex_syscall (6, futexp,                                         \
		     __lll_private_flag (FUTEX_WAKE_OP, my_private),       \
		     nr_wake, nr_wake2, futexp2,                        \
		     FUTEX_OP_CLEAR_WAKE_IF_GT_ONE)

/* Priority Inheritance support.  */
#define lll_futex_wait_requeue_pi(futexp, val, mutex, my_private) \
  lll_futex_timed_wait_requeue_pi (futexp, val, NULL, 0, mutex, my_private)

#define lll_futex_timed_wait_requeue_pi(futexp, val, timeout, clockbit, \
					mutex, my_private)                 \
  lll_futex_syscall (5, futexp,                                         \
		     __lll_private_flag (FUTEX_WAIT_REQUEUE_PI          \
					 | (clockbit), my_private),        \
		     val, timeout, mutex)


#define lll_futex_cmp_requeue_pi(futexp, nr_wake, nr_move, mutex,       \
				 val, my_private)                          \
  lll_futex_syscall (6, futexp,                                         \
		     __lll_private_flag (FUTEX_CMP_REQUEUE_PI,          \
					 my_private),                      \
		     nr_wake, nr_move, mutex, val)

#endif  /* !__ASSEMBLER__  */

#endif  /* lowlevellock-futex.h */
