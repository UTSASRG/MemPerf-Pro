#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
	int i, j;
	int * ptr;

	printf("alloc.cpp: in the beginning of main\n");

	for(i  = 0; i < 100000; i++) {
		ptr = (int *) malloc(sizeof(int));
		for(j = 0; j < 10000; j++) {
			*ptr = i * j;
			//usleep(10000);
		}
		free(ptr);
	}

	printf("exiting...\n");
	return EXIT_SUCCESS;
}
