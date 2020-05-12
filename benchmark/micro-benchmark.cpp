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
int nthreads = 40;    // Default number of threads.
int objSize = 9;
int allocationPerSeconds = 1;
int random_pause_max = 50;
double cpu_freq = 2327507.08008;
double cycles_per_allocation = cpu_freq * 1000000 / allocationPerSeconds;
double cycles_per_pause = 100;


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
    for (volatile int d = 0; d < (cycles_per_allocation - cycles_elasped) / cycles_per_pause; d++) {
        __pause();
    }
}

inline void random_pause() {
    int random_work = rand() % random_pause_max;
    for (volatile int d = 0; d < random_work; d++) {
        __pause();
    }
}

void new_allocation_worker() {
    Foo **total_new_allocations;
    total_new_allocations = new Foo *[nobjects * niterations];

    for (int i = 0; i < niterations; i++) {
        random_pause();
        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
            total_new_allocations[i * nobjects + j] = new Foo[objSize];
            assert (total_new_allocations[j]);
            rate_limit(rdtscp() - start);
        }
    }

    for (int i = 0; i < niterations; i++) {
        random_pause();
        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
            delete total_new_allocations[i * nobjects + j];
            rate_limit(rdtscp() - start);
        }
    }

    delete total_new_allocations;
}

void free_allocation_worker() {
    Foo **free_allocations;
    free_allocations = new Foo *[nobjects];

    //free allocation and deallocation
    for (int i = 0; i < niterations; i++) {
        random_pause();
        m
        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
            free_allocations[j] = new Foo[objSize];
            assert (free_allocations[j]);
            rate_limit(rdtscp() - start);
        }
        for (int j = 0; j < nobjects; j++) {
            unsigned long long start = rdtscp();
            free(free_allocations[j]);
            rate_limit(rdtscp() - start);
        }
    }
    delete free_allocations;
}


int main(int argc, char *argv[]) {
    thread **threads;

    if (argc >= 2) {
        cpu_freq = atoi(argv[1]);
    }

    if (argc >= 3) {
        nthreads = atoi(argv[2]);
    }

    if (argc >= 4) {
        objSize = atoi(argv[3]);
    }

    if (argc >= 5) {
        allocationPerSeconds = atoi(argv[4]);
    }

    if (argc >= 6) {
        nobjects = atoi(argv[5]);
    }

    if (argc >= 7) {
        niterations = atoi(argv[6]);
    }

    cycles_per_allocation = cpu_freq * 1000000 / allocationPerSeconds;
    unsigned start = rdtscp();
    __pause();
    cycles_per_pause = rdtscp() - start;

    srand((unsigned) rdtscp());

    printf("Running micro benchmark for %d threads, %d objSize, %d allocationPerSeconds, %lf cpu_freq , %d nobjects, %d iterations...\n",
           nthreads, objSize,
           allocationPerSeconds, cpu_freq, nobjects, niterations);

    threads = new thread *[nthreads];

    high_resolution_clock t;
    auto start = t.now();

    int i;
    // new allocation
    for (i = 0; i < nthreads; i++) {
        threads[i] = new thread(new_allocation_worker);
    }

    for (i = 0; i < nthreads; i++) {
        threads[i]->join();
    }

    for (i = 0; i < nthreads; i++) {
        delete threads[i];
    }

    // free allocation
    for (i = 0; i < nthreads; i++) {
        threads[i] = new thread(free_allocation_worker);
    }

    for (i = 0; i < nthreads; i++) {
        threads[i]->join();
    }

    for (i = 0; i < nthreads; i++) {
        delete threads[i];
    }

    auto stop = t.now();
    auto elapsed = duration_cast<duration<double>>(stop - start);

    cout << "Time elapsed = " << elapsed.count() << endl;

    delete[] threads;

    return 0;
}
