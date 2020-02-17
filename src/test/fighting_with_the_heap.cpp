//
// Created by 86152 on 2020/1/28.
//

#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

int main() {

    int *ptrs[10000];
    for(int i = 0; i < 10000; ++i) {
        ptrs[i] = (int *)malloc(sizeof(int));
    }
    for(int i = 0; i < 3; ++i) {
        printf("&ptrs[%d] = %p, ptrs[%d] = %p\n", i, &ptrs[i], i, ptrs[i]);
    }
    getchar();
    getchar();
    getchar();
    for(int i = 0; i < 10000; ++i) {
        free(ptrs[i]);
    }
//	getchar();
    return 0;
}
