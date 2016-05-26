#include <stdio.h>
#include <stdlib.h>

int main() {
	int i;
	int * ptr;

	printf("alloc.cpp: in the beginning of main\n");

	for(i  = 0; i < 100; i++) {
		ptr = (int *) malloc(sizeof(int));
		*ptr = i;
		free(ptr);
	}

	return EXIT_SUCCESS;
}
