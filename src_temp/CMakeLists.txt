cmake_minimum_required(VERSION 3.15)
project(mmprof)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)
include_directories(libtestperf)

add_executable(mmprof
        libtestperf/libtestperf.cpp
        libtestperf/libtestperf.h
        libtestperf/real.cpp
        libtestperf/real.hh
        libtestperf/test-bestcase.c
        libtestperf/test-worstcase.c
        libtestperf/xthread.hh
        prerun/libmallochelp.cpp
        prerun/runlib.cpp
        test/threadTest.cpp
        hashfuncs.hh
        hashmap.hh
        libmallocprof.cpp
        libmallocprof.h
        memsample.c
        memsample.h
        real.cpp
        real.hh
        recordfunctions.hh
        shadowmemory.cpp
        shadowmemory.hh
        spinlock.hh
        xthreadx.hh programstatus.cpp programstatus.h definevalues.h cmake-build-debug/mymalloc.cpp cmake-build-debug/mymalloc.h mymalloc.cpp allocatingstatus.cpp allocatingstatus.h threadlocalstatus.cpp threadlocalstatus.h)
