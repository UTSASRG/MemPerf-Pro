#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {
    int * ptr, *ptr2;
	int i, j;
    int tmp;
    //int* ptr = (int*)malloc(sizeof(int)*10000);


//    printf("addr i = %p, addr j = %p, ptr = %p\n", &i, &j, ptr);
    for(i = 0; i < 100; i++) {
        for(j = 0; j < 100; j++) {
            ptr = (int *) malloc(sizeof(int));
            //ptr2 = (int *) malloc(sizeof(int));
//            fprintf(stderr, "Malloc\n");
//            ptr[0] = i * j;
//            tmp = ptr[0];
//            tmp += ptr[0];
//            fprintf(stderr, "free\n");
            free(ptr);
            //free(ptr2);
        }
    }
//
//	getchar();
	return EXIT_SUCCESS;
}
