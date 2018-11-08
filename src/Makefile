SRCS =	libmallocprof.cpp	\
				real.cpp					\
				selfmap.cpp				\
				shadowmemory.cpp	\
				memsample.c

INCS =	xthreadx.hh			\
				memsample.h			\
				real.hh					\
				hashmap.hh			\
				list.hh					\
				hashfuncs.hh		\
				interval.hh			\
				selfmap.hh			\
				spinlock.hh			\
				shadowmemory.hh	\
				libmallocprof.h

DEPS = $(SRCS) $(INCS)

CC = gcc
CXX = g++ 
#CC = clang
#CXX = clang++ 

CFLAGS += -O2 -Wall -DNDEBUG --std=c++11 -g -fPIC -I$(ROOT)/include -fno-omit-frame-pointer
CFLAGS2 += -O0 -Wall -DNDEBUG --std=c++11 -g -fPIC -I$(ROOT)/include -fno-omit-frame-pointer -Wl,--no-as-needed

PROFILER_FLAGS += -D USE_THREAD_LOCAL
# PROFILER_FLAGS += -D MAPPINGS

#THREADTEST_FILE = threadTest.cpp
# THREADTEST_FILE = malloc-and-free.cpp
# TEST_FILE = malloc-and-free.cpp

THREADTEST_FILE = malloc-then-free.cpp
TEST_FILE = malloc-then-free.cpp

#No Blowup, all new addresses
NUM_THREADS = 20
NUM_MALLOCS = 80000

#CFLAGS += -DNO_PMU
#CFLAGS2 += -DNO_PMU

INCLUDE_DIRS = -I. -I/usr/include/x86_64-linux-gnu/c++/4.8/
LIBS     := dl pthread

TARGETS =	libmallocprof.so 			\
			test/test-clean 			\
			test/test-xlibc 			\
			test/test-obsd 				\
			test/test-freeguard 		\
			test/test-dieharder 		\
			test/test-tcmalloc			\
			test/test-tcmalloc-thread	\
			test/test-tcmalloc-noprof	\
			test/test-xlibc-thread 		\
			test/test-obsd-thread 		\
			test/test-freeguard-thread	\
			test/test-dieharder-thread	\
			test/test-tcmalloc-thread	\
			test/bs-obsd				\
			test/bs-libc				\
			test/bs-freeguard			\
			test/bs-dieharder			\
			test/pmu-libc-test			\
			test/pmu-obsd-test			\
			test/pmu-dieharder-test		\
			test/pmu-freeguard-test		\

default: threads

all: $(TARGETS)

single:		libmallocprof.so \
			test/test-xlibc \
			test/test-obsd \
			test/test-freeguard \
			test/test-dieharder \
			test/test-tcmalloc	\
			test/test-tcmalloc-noprof

threads:	libmallocprof.so \
			test/test-xlibc-thread \
			test/test-dieharder-thread \
			test/test-freeguard-thread \
			test/test-obsd-thread \
			test/test-tcmalloc-thread

pmu: test/pmu-libc-test test/pmu-obsd-test test/pmu-dieharder-test test/pmu-freeguard-test

# Libmallocprof
libmallocprof.so: $(DEPS)
	$(CXX) $(CFLAGS2) $(PROFILER_FLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmallocprof.so -ldl -lpthread

# TCMalloc 4.2.6
test/test-tcmalloc-noprof: test/$(TEST_FILE)
	$(CXX) $(CFLAGS2) -o test/test-tcmalloc-noprof test/$(TEST_FILE) -ltcmalloc

test/test-tcmalloc: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -o test/test-tcmalloc test/$(TEST_FILE) -ltcmalloc

test/test-tcmalloc-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -o test/test-tcmalloc-thread test/$(THREADTEST_FILE) -lpthread -ltcmalloc

# OpenBSD
test/test-obsd: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/test-obsd test/$(TEST_FILE)

test/test-obsd-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/test-obsd-thread test/$(THREADTEST_FILE) -lpthread

# FreeGuard
test/test-freeguard: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/FreeGuard/libfreeguard.so -o test/test-freeguard test/$(TEST_FILE)

test/test-freeguard-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/FreeGuard/libfreeguard.so -o test/test-freeguard-thread test/$(THREADTEST_FILE) -lpthread

# DieHarder
test/test-dieharder: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/DieHard-old/src/libdieharder.so -o test/test-dieharder test/$(TEST_FILE)

test/test-dieharder-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/DieHard-old/src/libdieharder.so -o test/test-dieharder-thread test/$(THREADTEST_FILE) -lpthread

# Libc
test/test-xlibc: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/test-xlibc test/$(TEST_FILE)

test/test-xlibc-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/test-xlibc-thread test/$(THREADTEST_FILE) -pthread

#Parsec Black-Scholes multithreaded
test/bs-libc: libmallocprof.so test/blackscholes-pthread.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/bs-libc test/blackscholes-pthread.cpp -lpthread -DENABLE_THREADS

test/bs-obsd: libmallocprof.so test/blackscholes-pthread.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/bs-obsd test/blackscholes-pthread.cpp -lpthread -DENABLE_THREADS

test/bs-dieharder: libmallocprof.so test/blackscholes-pthread.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/DieHard-old/src/libdieharder.so -o test/bs-dieharder test/blackscholes-pthread.cpp -lpthread -DENABLE_THREADS  

test/bs-freeguard: libmallocprof.so test/blackscholes-pthread.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/daniel/mem-alloc-project/FreeGuard/libfreeguard.so -o test/bs-freeguard test/blackscholes-pthread.cpp -lpthread -DENABLE_THREADS

#tests for pmu accuracy
test/pmu-libc-test: libmallocprof.so test/pmu-test.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/richard/Memoryallocators/libc-2.24/libmalloc.so -o test/pmu-libc-test test/pmu-test.cpp

test/pmu-obsd-test: libmallocprof.so test/pmu-test.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/pmu-obsd-test test/pmu-test.cpp

test/pmu-freeguard-test: libmallocprof.so test/pmu-test.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/daniel/mem-alloc-project/FreeGuard/libfreeguard.so -o test/pmu-freeguard-test test/pmu-test.cpp

test/pmu-dieharder-test: libmallocprof.so test/pmu-test.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmallocprof.so -rdynamic /home/sam/Memoryallocators/DieHard-old/src/libdieharder.so -o test/pmu-dieharder-test test/pmu-test.cpp

# Clean
test/test-clean: test/$(TEST_FILE)
	$(CXX) $(CFLAGS2) -o test/test-clean test/$(TEST_FILE)

clean:
	rm -f $(TARGETS)
	rm -f test/*.txt

# Executions
.PHONY: runThreads
runThreads:
	/usr/bin/time ./test/test-xlibc-thread $(NUM_THREADS) $(NUM_MALLOCS)
	/usr/bin/time ./test/test-dieharder-thread $(NUM_THREADS) $(NUM_MALLOCS)
	/usr/bin/time ./test/test-obsd-thread $(NUM_THREADS) $(NUM_MALLOCS)

.PHONY: runTC
runTC:
	./test/test-tcmalloc
