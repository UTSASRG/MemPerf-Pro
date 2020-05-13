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

int niterations = 50;    // Default number of iterations.
int nobjects = 3000000;  // Default number of objects.
int objSize = 9;

unsigned long long total_new_allocation_cycles;
unsigned long long total_new_free_cycles;
unsigned long long total_free_allocation_cycles;
unsigned long long total_free_deallocation_cycles;


class Foo {
public:
    Foo(void)
            : x(14),
              y(29) {}

    int x;
    int y;
};

int main(int argc, char *argv[]) {

    Foo **total_new_allocations;
    total_new_allocations = new Foo *[nobjects * niterations];
    total_new_allocation_cycles = 0;
    total_new_free_cycles = 0;

    for (int i = 0; i < niterations; i++) {
        unsigned long long start = rdtscp();
        for (int j = 0; j < nobjects; j++) {
//            fprintf(stderr, "thread index:%d, new allocate num:%d, time:%lf \n", thread_index, j, rdtscp() / CYCLES_PERSECOND);
            total_new_allocations[i * nobjects + j] = new Foo[objSize];
//            assert (total_new_allocations[i * nobjects + j]);
        }
        unsigned long long elasped = rdtscp() - start;
        total_new_allocation_cycles += elasped;
    }

    fprintf(stderr, "new allocate total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects,
            total_new_allocation_cycles,
            total_new_allocation_cycles / (niterations * nobjects));


    for (int i = 0; i < niterations; i++) {
        unsigned long long start = rdtscp();
        for (int j = 0; j < nobjects; j++) {
            free(total_new_allocations[i * nobjects + j]);
        }
        unsigned long long elasped = rdtscp() - start;
        total_new_free_cycles += elasped;
    }
    fprintf(stderr, "new deallocation call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects,
            total_new_free_cycles, total_new_free_cycles / (niterations * nobjects));

    delete total_new_allocations;


    Foo **free_allocations;
    free_allocations = new Foo *[nobjects];
    total_free_allocation_cycles = 0;
    total_free_deallocation_cycles = 0;

    //free allocation and deallocation
    for (int i = 0; i < niterations; i++) {
        unsigned long long start = rdtscp();
        for (int j = 0; j < nobjects; j++) {
            free_allocations[j] = new Foo[objSize];
//            assert (free_allocations[j]);
        }
        unsigned long long elasped = rdtscp() - start;
        total_free_allocation_cycles += elasped;

        start = rdtscp();
        for (int j = 0; j < nobjects; j++) {
            free(free_allocations[j]);
        }
        elasped = rdtscp() - start;
        total_free_deallocation_cycles += elasped;

    }
    delete free_allocations;

    fprintf(stderr, "free allocate total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects,
            total_free_allocation_cycles,
            total_free_allocation_cycles / (niterations * nobjects));

    fprintf(stderr, "free deallocation total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects,
            total_free_deallocation_cycles,
            total_free_deallocation_cycles / (niterations * nobjects));


//==============================================
    fprintf(stderr, "==============================================================================\n");

    total_new_allocations = new Foo *[nobjects * niterations];
    total_new_allocation_cycles = 0;
    total_new_free_cycles = 0;

    for (int i = 0; i < niterations; i++) {
        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
//            fprintf(stderr, "thread index:%d, new allocate num:%d, time:%lf \n", thread_index, j, rdtscp() / CYCLES_PERSECOND);
            total_new_allocations[i * nobjects + j] = new Foo[objSize];
//            assert (total_new_allocations[i * nobjects + j]);
            unsigned long long elasped = rdtscp() - start;
            total_new_allocation_cycles += elasped;
        }

    }

    fprintf(stderr, "new allocate total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects,
            total_new_allocation_cycles,
            total_new_allocation_cycles / (niterations * nobjects));


    for (int i = 0; i < niterations; i++) {
        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
            free(total_new_allocations[i * nobjects + j]);
            unsigned long long elasped = rdtscp() - start;
            total_new_free_cycles += elasped;
        }

    }
    fprintf(stderr, "new deallocation call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects,
            total_new_free_cycles, total_new_free_cycles / (niterations * nobjects));

    delete total_new_allocations;


    free_allocations = new Foo *[nobjects];
    total_free_allocation_cycles = 0;
    total_free_deallocation_cycles = 0;

    //free allocation and deallocation
    for (int i = 0; i < niterations; i++) {
        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
            free_allocations[j] = new Foo[objSize];
//            assert (free_allocations[j]);
            unsigned long long elasped = rdtscp() - start;
            total_free_allocation_cycles += elasped;
        }


        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
            free(free_allocations[j]);
            unsigned long long elasped = rdtscp() - start;
            total_free_deallocation_cycles += elasped;
        }


    }
    delete free_allocations;

    fprintf(stderr, "free allocate total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects,
            total_free_allocation_cycles,
            total_free_allocation_cycles / (niterations * nobjects));

    fprintf(stderr, "free deallocation total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects,
            total_free_deallocation_cycles,
            total_free_deallocation_cycles / (niterations * nobjects));


    return 0;
}

