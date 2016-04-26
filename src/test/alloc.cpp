#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

int main() {
	int i;
	int *ptr;

	printf("In the beginning of main\n");

	for(i  = 0; i < 10; i++) {
		ptr = (int *) malloc(10 * sizeof(int));
		*ptr = i;
		free(ptr);
	}

	return 0;
}
