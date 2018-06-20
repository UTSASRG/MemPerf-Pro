#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {

		  size_t size = 8;
		  void* pointer;

		  for (int i = 0; i < 200000; i++) {

					 pointer = malloc (size);
					 free (pointer);
					 size += 8;
		  }

		  return EXIT_SUCCESS;
}
