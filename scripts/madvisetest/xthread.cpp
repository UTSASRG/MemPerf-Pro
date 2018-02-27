#include "xthread.hh"

char * getThreadBuffer() {
	#ifdef CUSTOMIZED_STACK
	thread_t * current = xthread::getInstance().getThread(getThreadIndex(&current));
	#else
  	thread_t * current = xthread::getInstance().getThread();
	#endif
	return current->outputBuf;
}
