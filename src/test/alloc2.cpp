#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {

		  size_t size = 8;
		  void* pointer;

		  for (int i = 0; i < 10000; i++) {

					 pointer = malloc (size);
					 free (pointer);
		  }

		  return EXIT_SUCCESS;
}
