#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <malloc.h>
#include <cstdint>

void* thread_start (void*);

#define NUM_THREADS 1
#define NUM_MALLOCS 10000

void* thread_start (void*);

int main() {

	pthread_t threads[NUM_THREADS];
	int create, join;
	void* result;

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

	size_t size = 128;
	void* pointer[NUM_MALLOCS];
//	void* pointer;
	
	for (int i = 0; i < NUM_MALLOCS; i++) {
		pointer[i] = malloc (size);
//		pointer = malloc (size);
//		free (pointer);
	}

	for (int i = 0; i < NUM_MALLOCS; i++) {
		free (pointer[i]);
	}

//	getchar();
	return nullptr;
}
