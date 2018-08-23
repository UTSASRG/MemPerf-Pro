#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {

	int i, j;
	int* ptr;

	for(i  = 0; i < 10000; i++) {
		ptr = (int *) malloc(sizeof(int));
		for(j = 0; j < 10000; j++) {
			*ptr = i * j;
		}
		free(ptr);
	}
//	getchar();
	return EXIT_SUCCESS;
}
