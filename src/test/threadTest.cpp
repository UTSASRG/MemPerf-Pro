#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <malloc.h>

#define NUM_THREADS 20
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

	int i;
	int* j;
	int a;
	void* p;
	for (i = 0; i < NUM_MALLOCS; i++) 
		p = malloc (64);

	free (p);

	j = (int*) malloc (sizeof (int) * 1000);
	
	for (i = 0; i < 1000; i++) 
		j[i] = 50+i;

	for (i = 0; i < 1000; i++)
		a = j[i];

	return nullptr;
}
