#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SIZE 100000
volatile int array[SIZE];

int main() {
	int i, j;
	int * ptr;

	fprintf(stderr, "in the beginning of main\n");
	ptr = (int *) malloc(sizeof(int)*100000);

	for(i  = 0; i < 100000; i++) {
		for(j = 0; j < 10000; j++) {
			array[i]++;
		}
	}

	printf("exiting...\n");
	return EXIT_SUCCESS;
}
