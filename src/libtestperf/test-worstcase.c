#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_VALUES 8000000
#define FOUR_BYTES 0
#define EIGHT_BYTES 1
#define TWELVE_BYTES 2

int main(int argc, char *argv[]) {
	int i;
	char ** values = (char **)malloc(NUM_VALUES * sizeof(char *));

	for(i = 0; i < NUM_VALUES; i++) {
		values[i] = (char *)malloc(32);
		sprintf(values[i], "sam");
	}

	for(i = 0; i < NUM_VALUES; i++) {
		char * value = values[i];
		if(strcmp(value, "sam") != 0) {
			printf("ERROR: check failed\n");
			abort();
		}
		free(value);
	}

	free(values);
	return EXIT_SUCCESS;
}
