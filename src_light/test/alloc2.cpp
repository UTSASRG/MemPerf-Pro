#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <malloc.h>

int main() {

	size_t size = 128;
	void* pointer;
	int NUM_MALLOCS = 1;

	for (int i = 0; i < NUM_MALLOCS; i++) {
		pointer = malloc (size);
//		printf ("pointer= %p\n", pointer);
//		malloc_stats();
		free (pointer);
		size += 30;
	}

	return EXIT_SUCCESS;
}
