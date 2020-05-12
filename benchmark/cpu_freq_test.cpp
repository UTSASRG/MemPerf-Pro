//
// Created by XIN ZHAO on 5/11/20.
//
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

inline unsigned long long rdtscp() {
    unsigned int lo, hi;
    asm volatile (
    "rdtscp"
    : "=a"(lo), "=d"(hi) /* outputs */
    : "a"(0)             /* inputs */
    : "%ebx", "%ecx");     /* clobbers*/
    unsigned long long retval = ((unsigned long long) lo) | (((unsigned long long) hi) << 32);
    return retval;
}

inline void __pause() {
    volatile int f = 1;
    f = f + f;
    f = f * f;
    f = f + f;
    f = f * f;
}

int main(int argc, char *argv[]) {

    unsigned long long start = rdtscp();
    sleep(100);
    unsigned long long total_cycle = rdtscp() - start;
    fprintf(stderr, "total cycles in 100 seconds are %llu \n", total_cycle);

    start = rdtscp();
    for (int i = 0; i < 100; i++) {
        __pause();
    }
    total_cycle = rdtscp() - start;
    fprintf(stderr, "total cycles for 100 pauses are %llu \n", total_cycle);
}