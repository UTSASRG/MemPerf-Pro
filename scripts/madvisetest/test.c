#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096

int main(int argc, char *argv[]) {
		void * one_page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		int * object = (int *)one_page;
		printf("Writing to object %p...\n", object);
		*object = 1;

		printf("Calling madvise w/ MADV_DONTNEED for the page at %p...\n", one_page);
		if(madvise(one_page, PAGE_SIZE, MADV_DONTNEED) == -1) {
				perror("madvise failed");
		}

		printf("Rewriting object at %p...\n", object);
		*object = -1;

		return EXIT_SUCCESS;
}
