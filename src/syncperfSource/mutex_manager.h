#ifndef __MY_MUTEX__
#define __MY_MUTEX__

#include<pthread.h>
#include<assert.h>


#ifdef __cplusplus
extern "C" {
#endif
//
//#include "finetime.h"

#define MAX_CALL_STACK_DEPTH 6
#define MAX_NUM_STACKS 100
typedef double  WAIT_TIME_TYPE;
typedef unsigned int UINT32;


typedef struct {
	pthread_mutex_t mutex;

	// Keep the address of nominal mutex;
	pthread_mutex_t * nominalmutex;

	size_t entry_index; // mutex entry per thread index
	int stack_count; // how many different call sites
	size_t esp_offset[MAX_NUM_STACKS];
	size_t eip[MAX_NUM_STACKS];
	size_t stacks[MAX_NUM_STACKS][MAX_CALL_STACK_DEPTH+1];
}mutex_t;


//per thread data
typedef struct {

	UINT32 access_count;
	UINT32 fail_count;
	UINT32 cond_waits;
	UINT32 trylock_fail_count;
	WAIT_TIME_TYPE cond_futex_wait; // time spend in cond wait
	WAIT_TIME_TYPE futex_wait; // time spend for lock grabbing
}thread_mutex_t;

#ifdef __cplusplus
}
#endif


#endif
