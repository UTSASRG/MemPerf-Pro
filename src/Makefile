SRCS =   libmallocprof.cpp	\
			real.cpp		\
			selfmap.cpp		\
			memsample.c

INCS =	xthreadx.hh		\
			memsample.h		\
			real.hh			\
			hashmap.hh		\
			list.hh			\
			hashfuncs.hh	\
			interval.hh	\
			selfmap.hh	\
			spinlock.hh

DEPS = $(SRCS) $(INCS)

#CXX = g++ 
CXX = clang++ 

CFLAGS += -O2 -Wall -DNDEBUG --std=c++11 -g -fPIC -I$(ROOT)/include -fno-omit-frame-pointer
CFLAGS2 += -O0 -Wall -DNDEBUG --std=c++11 -g -fPIC -I$(ROOT)/include -fno-omit-frame-pointer

INCLUDE_DIRS = -I. -I/usr/include/x86_64-linux-gnu/c++/4.8/
LIBS     := dl pthread

TARGETS = libmallocprof.so test/test-clean test/test-obsd test/test-xlibc test/test-freeguard \
			 test/test-dieharder test/test-obsd-thread test/test-freeguard-thread \
			 test/test-dieharder-thread test/test-xlibc-thread test/bs-obsd test/bs-libc \
			 test/bs-freeguard test/bs-dieharder test/pmu-libc-test test/pmu-obsd-test \
			 test/pmu-dieharder-test test/pmu-freeguard-test

all: $(TARGETS)

threads: libmallocprof.so \
			test/test-xlibc-thread \
			test/test-dieharder-thread \
			test/test-freeguard-thread \
			test/test-obsd-thread

# Libmallocprof
libmallocprof.so: $(DEPS)
	$(CXX) $(CFLAGS2) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmallocprof.so -ldl -lpthread

# OpenBSD
test/test-obsd: libmallocprof.so test/alloc.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/test-obsd test/alloc.cpp

test/test-obsd-thread: libmallocprof.so test/threadTest.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/test-obsd-thread test/threadTest.cpp -lpthread

# FreeGuard
test/test-freeguard: libmallocprof.so test/alloc.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/FreeGuard/libfreeguard.so -o test/test-freeguard test/alloc.cpp

test/test-freeguard-thread: libmallocprof.so test/threadTest.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/FreeGuard/libfreeguard.so -o test/test-freeguard-thread test/threadTest.cpp -lpthread

# DieHarder
test/test-dieharder: libmallocprof.so test/alloc.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/DieHard/src/libdieharder.so -o test/test-dieharder test/alloc.cpp

test/test-dieharder-thread: libmallocprof.so test/threadTest.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/DieHard/src/libdieharder.so -o test/test-dieharder-thread test/threadTest.cpp -lpthread

# Libc
test/test-xlibc: libmallocprof.so test/alloc2.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/test-xlibc test/alloc2.cpp

test/test-xlibc-thread: libmallocprof.so test/threadTest.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/test-xlibc-thread test/threadTest.cpp -pthread

#Parsec Black-Scholes multithreaded
test/bs-libc: libmallocprof.so test/blackscholes-pthread.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/bs-libc test/blackscholes-pthread.cpp -lpthread -DENABLE_THREADS

test/bs-obsd: libmallocprof.so test/blackscholes-pthread.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/bs-obsd test/blackscholes-pthread.cpp -lpthread -DENABLE_THREADS

test/bs-dieharder: libmallocprof.so test/blackscholes-pthread.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/DieHard/src/libdieharder.so -o test/bs-dieharder test/blackscholes-pthread.cpp -lpthread -DENABLE_THREADS  

test/bs-freeguard: libmallocprof.so test/blackscholes-pthread.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/FreeGuard/libfreeguard.so -o test/bs-freeguard test/blackscholes-pthread.cpp -lpthread -DENABLE_THREADS

#tests for pmu accuracy
test/pmu-libc-test: libmallocprof.so test/pmu-test.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/pmu-libc-test test/pmu-test.cpp

test/pmu-obsd-test: libmallocprof.so test/pmu-test.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/pmu-obsd-test test/pmu-test.cpp

test/pmu-freeguard-test: libmallocprof.so test/pmu-test.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/FreeGuard/libfreeguard.so -o test/pmu-freeguard-test test/pmu-test.cpp

test/pmu-dieharder-test: libmallocprof.so test/pmu-test.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/DieHard/src/libdieharder.so -o test/pmu-dieharder-test test/pmu-test.cpp

# Clean
test/test-clean: test/alloc.cpp
	$(CXX) $(CFLAGS2) -o test/test-clean test/alloc.cpp

clean:
	rm -f $(TARGETS)
