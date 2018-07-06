#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <malloc.h>
#include <cstdint>

#define NUM_THREADS 2
#define NUM_MALLOCS 1000

void* thread_start (void*);
pthread_barrier_t barrier;

int main() {

	pthread_t threads[NUM_THREADS];
	int create, join;
	void* result;

	pthread_barrier_init (&barrier, NULL, (unsigned int) NUM_THREADS);

	for (int i = 0; i < NUM_THREADS; i++) {

		create = pthread_create (&threads[i], NULL, &thread_start, NULL);

		if (create != 0) {

			printf ("Error creating thread.\n");
			abort ();
		}
	}

	for (int i = 0; i < NUM_THREADS; i++) {

		join = pthread_join (threads[i], &result);

		if (join != 0) {

			printf ("Error joining thread.\n");
			abort ();
		}
	}

//	malloc_stats ();

	return EXIT_SUCCESS;
}

void* thread_start (void* arg) {

	void* pointer;
	
	for (int i = 0; i < NUM_MALLOCS; i++) {

		pointer = malloc (64);
		free (pointer);
	}

	return nullptr;
}
