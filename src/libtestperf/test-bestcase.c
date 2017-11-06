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
		switch(rand() % 3) {
			case FOUR_BYTES:
				values[i] = (char *)malloc(4);
				break;
			case EIGHT_BYTES:
				values[i] = (char *)malloc(8);
				break;
			case TWELVE_BYTES:
				values[i] = (char *)malloc(12);
				break;
		}
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
