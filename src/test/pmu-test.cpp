#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <malloc.h>
#include <cstdint>

//int NUM_THREADS;
int NUM_MALLOCS;

int main(int argc, char **argv) {

    if(argc != 2){
        printf("Wrong num of args\n");
        exit(1);
    }

    //NUM_THREADS = atoi(argv[1]);
    NUM_MALLOCS = atoi(argv[1]);

    for(int i = 0; i < NUM_MALLOCS; i++){
        //char *s1 = (char *) malloc(536870912);
        //char *s1 = (char *) malloc(16);
        //char *s1 = (char *) calloc(1, 536870912);
        char *s1 = (char *) calloc(1, 16);
        //char *s1 = (char *) realloc(NULL, 536870912);
        //char *s1 = (char *) realloc(NULL, 16);
        free(s1);
    }

	return EXIT_SUCCESS;
}
