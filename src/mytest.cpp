//
// Created by Jin Zhou on 1/23/20.
//
#include "memsample.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    int i, j, k=0;
    int * sum = (int*)malloc(sizeof(int)*100000);
    *sum = 0;
    printf("i add:%lld, j add:%lld, k add:%lld, sum add:%lld\n", &i, &j, &k, sum);
    initPMU();
    for(i = 0; i < 500; i++) {
        for(j = 0; j < 500; j++) {
            sum[rand()%100000] += i+j;
        }
    }
    stopSampling();
    free(sum);
    return 0;
}

