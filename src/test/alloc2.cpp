#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {

	size_t size = 128;
	void* pointer;

	for (int i = 0; i < 10000; i++) {
		pointer = malloc (size);
//		printf ("pointer= %p\n", pointer);
		free (pointer);
		size += 64;
	}

	return EXIT_SUCCESS;
}
