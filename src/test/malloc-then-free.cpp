#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <malloc.h>
#include <cstdint>

void* thread_start (void*);
pthread_barrier_t barrier;
int numThreads = 0;
int numMallocs = 0;

int main(int argc, char* argv[]) {

	if (argc != 3) {
		printf ("Usage: %s <numThreads> <numMallocs>\n", argv[0]);
		abort();
	}

	numThreads = atoi(argv[1];)
	numMallocs = atoi(argv[2]);

	pthread_t threads[numThreads];
	int create, join;
	void* result;

	pthread_barrier_init(&barrier, NULL, numThreads);

	for (int i = 0; i < numThreads; i++) {

		create = pthread_create (&threads[i], NULL, &thread_start, NULL);

		if (create != 0) {

			printf ("Error creating thread.\n");
			abort ();
		}
	}


	for (int i = 0; i < numThreads; i++) {

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

	size_t size;
	void* pointer[numMallocs];
	
	for (int i = 0; i < numMallocs; i++) {
		size = (size_t) (lrand48() % 1000) + 8;
		pointer[i] = malloc (size);
	}

	pthread_barrier_wait(&barrier);

	for (int i = 0; i < numMallocs; i++) {
		free (pointer[i]);
	}

	return nullptr;
}
