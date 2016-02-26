#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

int main() {

	int * ptr;
	fprintf(stderr, "\nIn the beginning of main\n");
	ptr = (int *) malloc(8);
	* ptr = 4;

	free(ptr);

}
