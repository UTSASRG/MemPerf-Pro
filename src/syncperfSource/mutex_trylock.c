#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include "pthreadP.h"
#include <lowlevellock.h>


#ifndef lll_trylock_elision
#define lll_trylock_elision(a,t) lll_trylock(a)
#endif

#ifndef FORCE_ELISION
#define FORCE_ELISION(m, s)
#endif

int
my_pthread_mutex_trylock (pthread_mutex_t *mutex) {

  int result =  do_mutex_trylock(mutex);
  return result;

}


int
do_mutex_trylock (pthread_mutex_t *mutex)
{
	int oldval;
	pid_t id = THREAD_GETMEM (THREAD_SELF, tid);

	switch (__builtin_expect (PTHREAD_MUTEX_TYPE_ELISION (mutex),
		PTHREAD_MUTEX_TIMED_NP))
	{
		/* Recursive mutex.  */
	case PTHREAD_MUTEX_RECURSIVE_NP|PTHREAD_MUTEX_ELISION_NP:
	case PTHREAD_MUTEX_RECURSIVE_NP:
		/* Check whether we already hold the mutex.  */
		if (mutex->__data.__owner == id)
		{
			/* Just bump the counter.  */
			if (__glibc_unlikely (mutex->__data.__count + 1 == 0))
				/* Overflow of the counter.  */
				return EAGAIN;

			++mutex->__data.__count;
			return 0;
		}

		if (lll_trylock (mutex->__data.__lock) == 0)
		{
			/* Record the ownership.  */
			mutex->__data.__owner = id;
			mutex->__data.__count = 1;
			++mutex->__data.__nusers;
			return 0;
		}
		break;
	case PTHREAD_MUTEX_TIMED_ELISION_NP:
		assert(PTHREAD_MUTEX_TYPE_ELISION (mutex) != PTHREAD_MUTEX_TIMED_ELISION_NP);

	case PTHREAD_MUTEX_TIMED_NP:
		FORCE_ELISION (mutex, goto elision);
		/*FALL THROUGH*/
	case PTHREAD_MUTEX_ADAPTIVE_NP:
	case PTHREAD_MUTEX_ERRORCHECK_NP:
		if (lll_trylock (mutex->__data.__lock) != 0)
			break;

		/* Record the ownership.  */
		mutex->__data.__owner = id;
		++mutex->__data.__nusers;

		return 0;

	case PTHREAD_MUTEX_ROBUST_RECURSIVE_NP:
	case PTHREAD_MUTEX_ROBUST_ERRORCHECK_NP:
	case PTHREAD_MUTEX_ROBUST_NORMAL_NP:
	case PTHREAD_MUTEX_ROBUST_ADAPTIVE_NP:
#ifdef __NR_futex
	case PTHREAD_MUTEX_PI_RECURSIVE_NP:
	case PTHREAD_MUTEX_PI_ERRORCHECK_NP:
	case PTHREAD_MUTEX_PI_NORMAL_NP:
	case PTHREAD_MUTEX_PI_ADAPTIVE_NP:
	case PTHREAD_MUTEX_PI_ROBUST_RECURSIVE_NP:
	case PTHREAD_MUTEX_PI_ROBUST_ERRORCHECK_NP:
	case PTHREAD_MUTEX_PI_ROBUST_NORMAL_NP:
	case PTHREAD_MUTEX_PI_ROBUST_ADAPTIVE_NP:
   #if 1
		{
			int kind = mutex->__data.__kind & PTHREAD_MUTEX_KIND_MASK_NP;
			int robust = mutex->__data.__kind & PTHREAD_MUTEX_ROBUST_NORMAL_NP;

			if (robust)
				/* Note: robust PI futexes are signaled by setting bit 0.  */
				THREAD_SETMEM (THREAD_SELF, robust_head.list_op_pending,
				(void *) (((uintptr_t) &mutex->__data.__list.__next)
				| 1));

			oldval = mutex->__data.__lock;

			/* Check whether we already hold the mutex.  */
			if (__glibc_unlikely ((oldval & FUTEX_TID_MASK) == id))
			{
				if (kind == PTHREAD_MUTEX_ERRORCHECK_NP)
				{
					THREAD_SETMEM (THREAD_SELF, robust_head.list_op_pending, NULL);
					return EDEADLK;
				}

				if (kind == PTHREAD_MUTEX_RECURSIVE_NP)
				{
					THREAD_SETMEM (THREAD_SELF, robust_head.list_op_pending, NULL);

					/* Just bump the counter.  */
					if (__glibc_unlikely (mutex->__data.__count + 1 == 0))
						/* Overflow of the counter.  */
						return EAGAIN;

					++mutex->__data.__count;

					return 0;
				}
			}

			oldval
				= atomic_compare_and_exchange_val_acq (&mutex->__data.__lock,
				id, 0);

			if (oldval != 0)
			{
				if ((oldval & FUTEX_OWNER_DIED) == 0)
				{
					THREAD_SETMEM (THREAD_SELF, robust_head.list_op_pending, NULL);

					return EBUSY;
				}

				assert (robust);

				/* The mutex owner died.  The kernel will now take care of
				everything.  */
				int private = (robust
					? PTHREAD_ROBUST_MUTEX_PSHARED (mutex)
					: PTHREAD_MUTEX_PSHARED (mutex));
				INTERNAL_SYSCALL_DECL (__err);
				int e = INTERNAL_SYSCALL (futex, __err, 4, &mutex->__data.__lock,
					__lll_private_flag (FUTEX_TRYLOCK_PI,
					private), 0, 0);

				if (INTERNAL_SYSCALL_ERROR_P (e, __err)
					&& INTERNAL_SYSCALL_ERRNO (e, __err) == EWOULDBLOCK)
				{
					THREAD_SETMEM (THREAD_SELF, robust_head.list_op_pending, NULL);

					return EBUSY;
				}

				oldval = mutex->__data.__lock;
			}

			if (__glibc_unlikely (oldval & FUTEX_OWNER_DIED))
			{
				atomic_and (&mutex->__data.__lock, ~FUTEX_OWNER_DIED);

				/* We got the mutex.  */
				mutex->__data.__count = 1;
				/* But it is inconsistent unless marked otherwise.  */
				mutex->__data.__owner = PTHREAD_MUTEX_INCONSISTENT;

				ENQUEUE_MUTEX (mutex);
				THREAD_SETMEM (THREAD_SELF, robust_head.list_op_pending, NULL);

				/* Note that we deliberately exit here.  If we fall
				through to the end of the function __nusers would be
				incremented which is not correct because the old owner
				has to be discounted.  */
				return EOWNERDEAD;
			}

			if (robust
				&& __builtin_expect (mutex->__data.__owner
				== PTHREAD_MUTEX_NOTRECOVERABLE, 0))
			{
				/* This mutex is now not recoverable.  */
				mutex->__data.__count = 0;

				INTERNAL_SYSCALL_DECL (__err);
				INTERNAL_SYSCALL (futex, __err, 4, &mutex->__data.__lock,
					__lll_private_flag (FUTEX_UNLOCK_PI,
					PTHREAD_ROBUST_MUTEX_PSHARED (mutex)),
					0, 0);

				THREAD_SETMEM (THREAD_SELF, robust_head.list_op_pending, NULL);
				return ENOTRECOVERABLE;
			}

			if (robust)
			{
				ENQUEUE_MUTEX_PI (mutex);
				THREAD_SETMEM (THREAD_SELF, robust_head.list_op_pending, NULL);
			}

			mutex->__data.__owner = id;
			++mutex->__data.__nusers;
			mutex->__data.__count = 1;

			return 0;
		}
  #endif //mejbah
#endif  /* __NR_futex.  */

	case PTHREAD_MUTEX_PP_RECURSIVE_NP:
	case PTHREAD_MUTEX_PP_ERRORCHECK_NP:
	case PTHREAD_MUTEX_PP_NORMAL_NP:
	case PTHREAD_MUTEX_PP_ADAPTIVE_NP:
		{
			int kind = mutex->__data.__kind & PTHREAD_MUTEX_KIND_MASK_NP;

			oldval = mutex->__data.__lock;

			/* Check whether we already hold the mutex.  */
			if (mutex->__data.__owner == id)
			{
				if (kind == PTHREAD_MUTEX_ERRORCHECK_NP)
					return EDEADLK;

				if (kind == PTHREAD_MUTEX_RECURSIVE_NP)
				{
					/* Just bump the counter.  */
					if (__glibc_unlikely (mutex->__data.__count + 1 == 0))
						/* Overflow of the counter.  */
						return EAGAIN;

					++mutex->__data.__count;

					return 0;
				}
			}

			int oldprio = -1, ceilval;
			do
			{
				int ceiling = (oldval & PTHREAD_MUTEX_PRIO_CEILING_MASK)
					>> PTHREAD_MUTEX_PRIO_CEILING_SHIFT;

				if (__pthread_current_priority () > ceiling)
				{
					if (oldprio != -1)
						__pthread_tpp_change_priority (oldprio, -1);
					return EINVAL;
				}

				int retval = __pthread_tpp_change_priority (oldprio, ceiling);
				if (retval)
					return retval;

				ceilval = ceiling << PTHREAD_MUTEX_PRIO_CEILING_SHIFT;
				oldprio = ceiling;

				oldval
					= atomic_compare_and_exchange_val_acq (&mutex->__data.__lock,
					ceilval | 1, ceilval);

				if (oldval == ceilval)
					break;
			}
			while ((oldval & PTHREAD_MUTEX_PRIO_CEILING_MASK) != ceilval);

			if (oldval != ceilval)
			{
				__pthread_tpp_change_priority (oldprio, -1);
				break;
			}

			assert (mutex->__data.__owner == 0);
			/* Record the ownership.  */
			mutex->__data.__owner = id;
			++mutex->__data.__nusers;
			mutex->__data.__count = 1;

			return 0;
		}
		break;

	default:
		/* Correct code cannot set any other type.  */
		return EINVAL;
	}

	return EBUSY;
}
