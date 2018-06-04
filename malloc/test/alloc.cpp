#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {
//	int i, j;
	int * ptr;

	printf("alloc.cpp: in the beginning of main\n");

/*
	for(i  = 0; i < 100000; i++) {
		ptr = (int *) malloc(sizeof(int));
		for(j = 0; j < 10000; j++) {
			*ptr = i * j;
			//usleep(10000);
		}
		free(ptr);
	}
*/

	ptr = (int *) malloc(15);
	printf("malloc(15) = %p\n", ptr);

	ptr = (int *) malloc(1500000);
	printf("malloc(a bunch) = %p\n", ptr);

/*
	char * blah = (char *)mmap(NULL, 20000000, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	for(i = 0; i < 4882; i++) {
			char * ptr = blah + i * 4096;
			*ptr = 'a';
	}
*/

	printf("exiting...\n");
	return EXIT_SUCCESS;
}
