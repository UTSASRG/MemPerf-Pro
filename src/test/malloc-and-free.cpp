#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <malloc.h>
#include <cstdint>

void* thread_start (void*);
int numThreads = 0;
int numMallocs = 0;
int totalJoins = 0;

int main(int argc, char* argv[]) {

    printf("start\n");

//    for(int i = 0; i < 10000; ++i) {
//        int * ptr = (int *)malloc(4);
//        free(ptr);
//    }

    printf("multi\n");

	if (argc != 3) {
		printf ("Usage: %s <numThreads> <numMallocs>\n", argv[0]);
		abort();
	}

	numThreads = atoi(argv[1]);
	numMallocs = atoi(argv[2]);

	pthread_t threads[numThreads];
	int order[numThreads];
	int create, join;
	void* result;

	for (int i = 0; i < numThreads; i++) {

		order[i] = i;
		create = pthread_create (&threads[i], NULL, &thread_start, &order[i]);

		if (create != 0) {

            fprintf(stderr, "Error creating thread.\n");
            abort();
        }
	}


	for (int i = 0; i < numThreads; i++) {

		join = pthread_join (threads[i], &result);
		totalJoins++;

		if (join != 0) { 
			fprintf (stderr, "Error joining thread.\n");
			abort ();
		}
		fprintf(stderr, "Thread %d has joined\n", i);
		fprintf(stderr, "totalJoins = %d\n", totalJoins);
	}

	printf("single\n");

    for(int i = 0; i < 10000; ++i) {
        int * ptr = (int *)malloc(40);
        free(ptr);
    }

    printf("finish\n");

	return EXIT_SUCCESS;
}

void* thread_start (void* arg) {
    size_t size;
    void* pointer[numMallocs];
    for (int i = 0; i < numMallocs; i++) {
        size = (size_t) (lrand48() % 1000) + 8;
        pointer[i] = malloc (size);
    }
    for (int i = 0; i < numMallocs; i++) {
        free (pointer[i]);
    }
	fprintf(stderr, "Thread %d: I have finished my routine!\n", *((int*)arg));
    return nullptr;
}
