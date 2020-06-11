#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {
    fprintf(stderr, "start\n");

    for(int i = 0; i < 100000; ++i) {

        int * ptr = (int *) malloc(4000);

    }

    fprintf(stderr, "finished\n");
    return EXIT_SUCCESS;
}
