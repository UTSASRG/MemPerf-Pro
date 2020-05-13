//
// Created by XIN ZHAO on 5/13/20.
//

///-*-C++-*-//////////////////////////////////////////////////////////////////
//
// Hoard: A Fast, Scalable, and Memory-Efficient Allocator
//        for Shared-Memory Multiprocessors
// Contact author: Emery Berger, http://www.cs.utexas.edu/users/emery
//
// Copyright (c) 1998-2000, The University of Texas at Austin.
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as
// published by the Free Software Foundation, http://www.fsf.org.
//
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
//////////////////////////////////////////////////////////////////////////////



#ifndef _REENTRANT
#define _REENTRANT
#endif

#include <iostream>
#include <thread>
#include <chrono>

using namespace std;
using namespace std::chrono;

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

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

int niterations = 5000;    // Default number of iterations.

#define PAGE_SIZE 0x1000

inline void *mmap() {
    void *ptr = mmap(NULL, PAGE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "Couldn't do mmap \n");
        exit(-1);
    }

    return ptr;
}

inline void munmap(void *startaddr) {
    munmap(startaddr, PAGE_SIZE);
}

inline void madvise(void *startaddr) {
    madvise(startaddr, PAGE_SIZE, MADV_DONTNEED);
}

inline void mprotect(void *startaddr) {
    mprotect(startaddr, PAGE_SIZE, PROT_READ);
}


int main(int argc, char *argv[]) {

    void **toal_mapped_address;
    toal_mapped_address = new void *[niterations];
    unsigned long long total_cycles = 0;
    unsigned long long start = 0;

    start = rdtscp();
    for (int i = 0; i < niterations; i++) {
        toal_mapped_address[i] = mmap();
    }
    total_cycles = rdtscp() - start;
    fprintf(stderr, "mmap total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations,
            total_cycles,
            total_cycles / niterations);

    //==================================
    start = rdtscp();
    for (int i = 0; i < niterations; i++) {
        mprotect(toal_mapped_address[i]);
    }
    total_cycles = rdtscp() - start;
    fprintf(stderr, "mprotect total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations,
            total_cycles,
            total_cycles / niterations);
    //===================================
    start = rdtscp();
    for (int i = 0; i < niterations; i++) {
        madvise(toal_mapped_address[i]);
    }
    total_cycles = rdtscp() - start;
    fprintf(stderr, "madvise total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations,
            total_cycles,
            total_cycles / niterations);

    //===================================
    start = rdtscp();
    for (int i = 0; i < niterations; i++) {
        munmap(toal_mapped_address[i]);
    }
    total_cycles = rdtscp() - start;
    fprintf(stderr, "munmap total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations,
            total_cycles,
            total_cycles / niterations);

    //===================================
    start = rdtscp();
    for (int i = 0; i < niterations; i++) {
        sbrk(0x3100);
    }
    total_cycles = rdtscp() - start;
    fprintf(stderr, "sbrk total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations,
            total_cycles,
            total_cycles / niterations);

    //===================================
    start = rdtscp();
    for (int i = 0; i < niterations; i++) {
        rdtscp();
    }
    total_cycles = rdtscp() - start;
    fprintf(stderr, "rdtscp total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations,
            total_cycles,
            total_cycles / niterations);


    return 0;
}

