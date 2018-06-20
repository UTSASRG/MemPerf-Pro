#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>

#define NUM_THREADS 50

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

	return EXIT_SUCCESS;
}

void* thread_start (void* arg) {

	int i, j;
	int* ptr;
	for (i = 0; i < 1000; i++) {

		ptr = (int *) malloc(sizeof(int));
		for(j = 0; j < 1000; j++) {
			*ptr = i * j;
		}
		free(ptr);
	}

	return nullptr;
}
