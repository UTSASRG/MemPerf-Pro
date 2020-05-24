#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {
    int * ptr;
    ptr = (int *) malloc(6);
    free(ptr);
	return EXIT_SUCCESS;
}
