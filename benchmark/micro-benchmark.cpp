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
int nobjects = 30000;  // Default number of objects.
int nthreads = 1;    // Default number of threads.
int objSize = 9;
int allocationPerSeconds = 1;
int random_pause_max = 50;
double cycles_per_allocation = CYCLES_PERSECOND / allocationPerSeconds;


unsigned long long *total_new_allocation_cycles;
unsigned long long *total_new_free_cycles;
unsigned long long *total_free_allocation_cycles;
unsigned long long *total_free_deallocation_cycles;


class Foo {
public:
    Foo(void)
            : x(14),
              y(29) {}

    int x;
    int y;
};

inline void __pause() {
    volatile int f = 1;
    f = f + f;
    f = f * f;
    f = f + f;
    f = f * f;
}

inline void rate_limit(unsigned long long cycles_elasped) {
    if (cycles_per_allocation < cycles_elasped) {
        fprintf(stderr, "ERROR: allocation freq is too high\n");
    }
    for (volatile int d = 0; d < (cycles_per_allocation - cycles_elasped) / CYCLES_PER_PAUSE; d++) {
        __pause();
    }
}

inline void random_pause() {
    int random_work = rand() % random_pause_max;
    for (volatile int d = 0; d < random_work; d++) {
        __pause();
    }
}

void new_allocation_worker(int thread_index) {
    Foo **total_new_allocations;
    total_new_allocations = new Foo *[nobjects * niterations];
    total_new_allocation_cycles[thread_index] = 0;
    total_new_free_cycles[thread_index] = 0;

    for (int i = 0; i < niterations; i++) {
        random_pause();
        for (int j = 0; j < nobjects; j++) {
//            fprintf(stderr, "thread index:%d, new allocate num:%d, time:%lf \n", thread_index, j, rdtscp() / CYCLES_PERSECOND);
            unsigned long long start = rdtscp();
            total_new_allocations[i * nobjects + j] = new Foo[objSize];
            assert (total_new_allocations[i * nobjects + j]);
            unsigned long long elasped = rdtscp() - start;
            total_new_allocation_cycles[thread_index] += elasped;
            rate_limit(elasped);
        }
    }

    fprintf(stderr, "thread_index:%d, new allocate total call num:%d, total cycles:%llu, average cycles:%llu \n",
            thread_index, niterations * nobjects,
            total_new_allocation_cycles[thread_index],
            total_new_allocation_cycles[thread_index] / (niterations * nobjects));


    for (int i = 0; i < niterations; i++) {
        random_pause();
        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
            free(total_new_allocations[i * nobjects + j]);
            unsigned long long elasped = rdtscp() - start;
            total_new_free_cycles[thread_index] += elasped;
            rate_limit(elasped);
        }
    }
    fprintf(stderr, "thread_index:%d, new deallocation call num:%d, total cycles:%llu, average cycles:%llu \n",
            thread_index, niterations * nobjects,
            total_new_free_cycles[thread_index], total_new_free_cycles[thread_index] / (niterations * nobjects));

    delete total_new_allocations;
}

void free_allocation_worker(int thread_index) {
    Foo **free_allocations;
    free_allocations = new Foo *[nobjects];
    total_free_allocation_cycles[thread_index] = 0;
    total_free_deallocation_cycles[thread_index] = 0;

    //free allocation and deallocation
    for (int i = 0; i < niterations; i++) {
        random_pause();
        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
            free_allocations[j] = new Foo[objSize];
            assert (free_allocations[j]);
            unsigned long long elasped = rdtscp() - start;
            total_free_allocation_cycles[thread_index] += elasped;
            rate_limit(elasped);
        }
        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
            free(free_allocations[j]);
            unsigned long long elasped = rdtscp() - start;
            total_free_deallocation_cycles[thread_index] += elasped;
            rate_limit(rdtscp() - start);
        }
    }
    delete free_allocations;

    fprintf(stderr, "thread_index:%d, free allocate total call num:%d, total cycles:%llu, average cycles:%llu \n",
            thread_index, niterations * nobjects,
            total_free_allocation_cycles[thread_index],
            total_free_allocation_cycles[thread_index] / (niterations * nobjects));

    fprintf(stderr, "thread_index:%d, free deallocation total call num:%d, total cycles:%llu, average cycles:%llu \n",
            thread_index, niterations * nobjects,
            total_free_deallocation_cycles[thread_index],
            total_free_deallocation_cycles[thread_index] / (niterations * nobjects));

}


int main(int argc, char *argv[]) {
    thread **threads;

    if (argc >= 2) {
        nthreads = atoi(argv[1]);
    }

    if (argc >= 3) {
        objSize = atoi(argv[2]);
    }

    if (argc >= 4) {
        allocationPerSeconds = atoi(argv[3]);
    }

    if (argc >= 5) {
        nobjects = atoi(argv[4]);
    }

    if (argc >= 6) {
        niterations = atoi(argv[5]);
    }

    cycles_per_allocation = CYCLES_PERSECOND / allocationPerSeconds;

    srand((unsigned) rdtscp());

    total_new_allocation_cycles = (unsigned long long *) malloc(nthreads * sizeof(unsigned long long));
    total_new_free_cycles = (unsigned long long *) malloc(nthreads * sizeof(unsigned long long));
    total_free_allocation_cycles = (unsigned long long *) malloc(nthreads * sizeof(unsigned long long));
    total_free_deallocation_cycles = (unsigned long long *) malloc(nthreads * sizeof(unsigned long long));

    printf("Running micro benchmark for %d threads, %d objSize, %d allocationPerSeconds, %lf CYCLES_PERSECOND , %lf CYCLES_PER_PAUSE, %d nobjects, %d iterations...\n",
           nthreads, objSize,
           allocationPerSeconds, CYCLES_PERSECOND, CYCLES_PER_PAUSE, nobjects, niterations);

    threads = new thread *[nthreads];


    fprintf(stderr, "new allocate work start \n");

    int i;
    // new allocation
    for (i = 0; i < nthreads; i++) {
        threads[i] = new thread(new_allocation_worker, i);
    }

    for (i = 0; i < nthreads; i++) {
        threads[i]->join();
    }

    for (i = 0; i < nthreads; i++) {
        delete threads[i];
    }

    fprintf(stderr, "free allocate work start \n");

    // free allocation
    for (i = 0; i < nthreads; i++) {
        threads[i] = new thread(free_allocation_worker, i);
    }

    for (i = 0; i < nthreads; i++) {
        threads[i]->join();
    }

    for (i = 0; i < nthreads; i++) {
        delete threads[i];
    }

    delete[] threads;

    unsigned long long final_total_new_allocation_cycles = 0;
    unsigned long long final_total_new_free_cycles = 0;
    unsigned long long final_total_free_allocation_cycles = 0;
    unsigned long long final_total_free_deallocation_cycles = 0;
    for (i = 0; i < nthreads; i++) {
        final_total_new_allocation_cycles += total_new_allocation_cycles[i];
        final_total_new_free_cycles += total_new_free_cycles[i];
        final_total_free_allocation_cycles += total_free_allocation_cycles[i];
        final_total_free_deallocation_cycles += total_free_deallocation_cycles[i];
    }

    fprintf(stderr, "final new allocate total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects * nthreads,
            final_total_new_allocation_cycles,
            final_total_new_allocation_cycles / (niterations * nobjects * nthreads));

    fprintf(stderr, "final new deallocation call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects * nthreads,
            final_total_new_free_cycles,
            final_total_new_free_cycles / (niterations * nobjects * nthreads));

    fprintf(stderr, "final free allocate total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects * nthreads,
            final_total_free_allocation_cycles,
            final_total_free_allocation_cycles / (niterations * nobjects * nthreads));

    fprintf(stderr, "final free deallocation total call num:%d, total cycles:%llu, average cycles:%llu \n",
            niterations * nobjects * nthreads,
            final_total_free_deallocation_cycles,
            final_total_free_deallocation_cycles / (niterations * nobjects * nthreads));

    return 0;
}

