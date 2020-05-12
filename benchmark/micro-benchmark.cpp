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
int objSize = 1;
int allocationPerSeconds = 1;
int random_pause_max = 50;


class Foo {
public:
    Foo(void)
            : x(14),
              y(29) {}

    int x;
    int y;
};

void wait() {
    for (volatile int d = 0; d < allocationPerSeconds; d++) {
        volatile int f = 1;
        f = f + f;
        f = f * f;
        f = f + f;
        f = f * f;
    }
}

void random_pause() {
    int random_work = rand() % random_pause_max;
    for (volatile int d = 0; d < random_work; d++) {
        volatile int f = 1;
        f = f + f;
        f = f * f;
        f = f + f;
        f = f * f;
    }
}

void new_allocation_worker() {
    Foo **total_new_allocations;
    total_new_allocations = new Foo *[nobjects * niterations];

    for (int i = 0; i < niterations; i++) {
        random_pause();
        for (int j = 0; j < nobjects; j++) {
            total_new_allocations[i * nobjects + j] = new Foo[objSize];
            assert (a[j]);
            wait();
        }
    }

    for (int i = 0; i < niterations; i++) {
        random_pause();
        for (int j = 0; j < nobjects; j++) {
            delete total_new_allocations[i * nobjects + j];
            wait();
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
        for (int j = 0; j < nobjects; j++) {
            total_new_allocations[j] = new Foo[objSize];
            assert (a[j]);
            wait();
        }
        for (int j = 0; j < nobjects; j++) {
            free(total_new_allocations[j]);
            wait();
        }
    }
    delete free_allocations;
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
    srand((unsigned) rdtscp());

    printf("Running micro benchmark for %d threads, %d objSize, %d allocationPerSeconds, %d nobjects, %d iterations...\n",
           nthreads, objSize,
           allocationPerSeconds, nobjects, niterations);

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
