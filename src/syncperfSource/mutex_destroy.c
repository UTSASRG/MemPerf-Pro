#include <errno.h>
#include "pthreadP.h"
#include "mutex_manager.h"

//#include <stap-probe.h>


int
pthread_mutex_destroy (mutex)
         pthread_mutex_t *mutex;
{

      if ((mutex->__data.__kind & PTHREAD_MUTEX_ROBUST_NORMAL_NP) == 0
                      && mutex->__data.__nusers != 0)
                return EBUSY;
      /* Set to an invalid value.  */
      mutex->__data.__kind = -1;
      return 0;
}

