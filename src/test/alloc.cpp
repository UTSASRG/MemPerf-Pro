#include <stdio.h>
#include <stdlib.h>

int main() {
	int i, j;
	int * ptr;

	printf("alloc.cpp: in the beginning of main\n");

	for(i  = 0; i < 100; i++) {
		ptr = (int *) malloc(sizeof(int));
		for(j = 0; j < 100000; j++)
			*ptr = i * j;
		//free(ptr);
	}
	free(ptr);		// free last object used

	printf("exiting...\n");
	return EXIT_SUCCESS;
}
