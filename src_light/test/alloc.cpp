#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include <pthread.h>

int main() {
    fprintf(stderr, "start\n");

    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
    fprintf(stderr, "here\n");

    for(int i = 0; i < 100000; ++i) {

        int * ptr = (int *) malloc(4000);

    }

    fprintf(stderr, "finished\n");
    return EXIT_SUCCESS;
}
