#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <malloc.h>
#include <cstdint>

#define NUM_THREADS 16

void* thread_start (void*);

int main() {

	pthread_t * threads[NUM_THREADS];
	void* result;

	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_create (&threads[i], NULL, &thread_start, NULL);
	}


	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_join (threads[i], &result);
	}

	return 0;
}

void* thread_start (void* arg) {

    bool * fields = (bool *)malloc(sizeof(bool) * 1024);
    while (1) {
        for(int i = 0; i < 1024; ++i) {
            fields[i]++;
        }
    }
    for(int i = 0; i < 1024; ++i) {
        fprintf(stderr, "%u\n", fields[i]);
    }
	return nullptr;
}
