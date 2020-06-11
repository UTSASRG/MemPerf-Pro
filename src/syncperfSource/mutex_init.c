#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <lowlevellock.h>
#include <pthread.h>
//#include <kernel-features.h>
#include <atomic.h>
#include "pthreadP.h"
#include <sysdep.h>

#include<mutex_manager.h>

static const struct pthread_mutexattr default_mutexattr = {
	/* Default is a normal mutex, not shared between processes.  */
	.mutexkind = PTHREAD_MUTEX_NORMAL
};

int pthread_mutex_init (pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
	const struct pthread_mutexattr *imutexattr;

	assert (sizeof (pthread_mutex_t) <= __SIZEOF_PTHREAD_MUTEX_T);
	imutexattr = ((const struct pthread_mutexattr *) mutexattr
		?: &default_mutexattr);
	/* Sanity checks.  */
	switch (__builtin_expect (imutexattr->mutexkind
		& PTHREAD_MUTEXATTR_PROTOCOL_MASK,
		PTHREAD_PRIO_NONE
		<< PTHREAD_MUTEXATTR_PROTOCOL_SHIFT))
	{
	case PTHREAD_PRIO_NONE << PTHREAD_MUTEXATTR_PROTOCOL_SHIFT:
		break;
	case PTHREAD_PRIO_INHERIT << PTHREAD_MUTEXATTR_PROTOCOL_SHIFT:
		break;
	default:
		/* XXX: For now we don't support robust priority protected mutexes.  */
		if (imutexattr->mutexkind & PTHREAD_MUTEXATTR_FLAG_ROBUST)
			return ENOTSUP;
		break;
	}
	/* Clear the whole variable.  */
	memset (mutex, '\0', __SIZEOF_PTHREAD_MUTEX_T);

	/* Copy the values from the attribute.  */
	mutex->__data.__kind = imutexattr->mutexkind & ~PTHREAD_MUTEXATTR_FLAG_BITS;

	if ((imutexattr->mutexkind & PTHREAD_MUTEXATTR_FLAG_ROBUST) != 0)
	{
#ifndef __ASSUME_SET_ROBUST_LIST
		// if ((imutexattr->mutexkind & PTHREAD_MUTEXATTR_FLAG_PSHARED) != 0
		//         && __set_robust_list_avail < 0)
		//     return ENOTSUP;
#endif
		mutex->__data.__kind |= PTHREAD_MUTEX_ROBUST_NORMAL_NP;
	}
	switch (imutexattr->mutexkind & PTHREAD_MUTEXATTR_PROTOCOL_MASK)
	{
	case PTHREAD_PRIO_INHERIT << PTHREAD_MUTEXATTR_PROTOCOL_SHIFT:
		mutex->__data.__kind |= PTHREAD_MUTEX_PRIO_INHERIT_NP;
		break;
	case PTHREAD_PRIO_PROTECT << PTHREAD_MUTEXATTR_PROTOCOL_SHIFT:
		mutex->__data.__kind |= PTHREAD_MUTEX_PRIO_PROTECT_NP;
		int ceiling = (imutexattr->mutexkind
			& PTHREAD_MUTEXATTR_PRIO_CEILING_MASK)
			>> PTHREAD_MUTEXATTR_PRIO_CEILING_SHIFT;
		mutex->__data.__lock = ceiling << PTHREAD_MUTEX_PRIO_CEILING_SHIFT;
		break;
	default:
		break;
	}

	/* The kernel when waking robust mutexes on exit never uses
	* FUTEX_PRIVATE_FLAG FUTEX_WAKE.  */
	if ((imutexattr->mutexkind & (PTHREAD_MUTEXATTR_FLAG_PSHARED
		| PTHREAD_MUTEXATTR_FLAG_ROBUST)) != 0)
		mutex->__data.__kind |= PTHREAD_MUTEX_PSHARED_BIT;
                
	return 0;

}
