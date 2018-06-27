SRCS =  libmemperf.cpp	\
		real.cpp		\
		selfmap.cpp		\
		memsample.c

INCS =  xthread.hh		\
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

TARGETS = libmemperf.so test/test-clean test/test-obsd test/test-xlibc test/test-freeguard \
			 test/test-dieharder test/test-obsd-thread test/test-freeguard-thread \
			 test/test-dieharder-thread test/test-xlibc-thread test/bs-obsd test/bs-libc \
			 test/bs-freeguard test/bs-dieharder

all: $(TARGETS)

# Libmemperf
libmemperf.so: $(DEPS)
	$(CXX) $(CFLAGS2) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmemperf.so -ldl -lpthread

# OpenBSD
test/test-obsd: libmemperf.so test/alloc.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/test-obsd test/alloc.cpp

test/test-obsd-thread: libmemperf.so test/threadTest.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/test-obsd-thread test/threadTest.cpp -lpthread

# FreeGuard
test/test-freeguard: libmemperf.so test/alloc.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/richard/FreeGuard/libfreeguard.so -o test/test-freeguard test/alloc.cpp

test/test-freeguard-thread: libmemperf.so test/threadTest.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/richard/FreeGuard/libfreeguard.so -o test/test-freeguard-thread test/threadTest.cpp -lpthread

# DieHarder
test/test-dieharder: libmemperf.so test/alloc.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/sam/Memoryallocators/DieHard/src/libdieharder.so -o test/test-dieharder test/alloc.cpp

test/test-dieharder-thread: libmemperf.so test/threadTest.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/sam/Memoryallocators/DieHard/src/libdieharder.so -o test/test-dieharder-thread test/threadTest.cpp -lpthread

# Libc
test/test-xlibc: libmemperf.so test/alloc2.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/test-xlibc test/alloc2.cpp

test/test-xlibc-thread: libmemperf.so test/threadTest.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/test-xlibc-thread test/threadTest.cpp -pthread

#Parsec Black-Scholes
test/bs-libc: libmemperf.so test/blackscholes.c
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/bs-libc test/blackscholes.c -pthread 

test/bs-obsd: libmemperf.so test/blackscholes.c
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/bs-libc test/blackscholes.c -pthread 

test/bs-dieharder: libmemperf.so test/blackscholes.c
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/sam/Memoryallocators/DieHard/src/libdieharder.so -o test/bs-dieharder test/blackscholes.c -pthread 

test/bs-freeguard: libmemperf.so test/blackscholes.c
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/richard/FreeGuard/libfreeguard.so -o test/bs-freeguard test/blackscholes.c -pthread

# Clean
test/test-clean: test/alloc.cpp
	$(CXX) $(CFLAGS2) -o test/test-clean test/alloc.cpp

clean:
	rm -f $(TARGETS)
