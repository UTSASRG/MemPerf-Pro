SRCS = 	\
		libmallocprof.cpp \
		allocatingstatus.cpp \
		globalstatus.cpp \
		memsample.c \
		mymalloc.cpp \
		objTable.cpp \
		predictor.cpp \
		programstatus.cpp \
		real.cpp \
		shadowmemory.cpp \
		structs.cpp \
		threadlocalstatus.cpp \
		xthreadx.cpp

INCS = \
		libmallocprof.h \
		allocatingstatus.h \
		definevalues.h \
		globalstatus.h \
		hashfuncs.hh \
		hashlist.hh \
		hashmap.hh \
		memsample.h \
		mymalloc.h \
		objTable.h \
		predictor.h \
		privateheap.hh \
		programstatus.h \
		real.hh \
		shadowmemory.hh \
		spinlock.hh \
		structs.h \
		threadlocalstatus.h \
		xthreadx.hh

DEPS = $(SRCS) $(INCS)

CC = gcc
CXX = g++

CFLAGS += -pipe -g -g3 -ggdb3 -Wall -DNDEBUG --std=c++11 -fPIC -I$(ROOcpu_set_t T)/include -Wno-unused-result -fno-omit-frame-pointer -Wl,--no-as-needed -lrt
CFLAGS2 += -O2 -pipe -g -g3 -ggdb3 -Wall -DNDEBUG --std=c++11 -fPIC -I$(ROOT)/include -Wno-unused-result -fno-omit-frame-pointer -Wl,--no-as-needed -lrt

THREADTEST_FILE = malloc-and-free.cpp
TEST_FILE = alloc.cpp

#CFLAGS += -DNO_PMU
#CFLAGS2 += -DNO_PMU

INCLUDE_DIRS = -I.
LIBS     := dl pthread


TARGETS = libmallocprof.so


# Libmallocprof
libmallocprof.so: $(DEPS)
	$(CXX) $(CFLAGS2) $(PROFILER_FLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmallocprof.so -ldl -lpthread

test/test-tcmalloc-minimal: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS) -o test/test-tcmalloc-minimal test/$(TEST_FILE) -rdynamic ./libmallocprof.so -ltcmalloc_minimal

test/test-tcmalloc-minimal-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -o test/test-tcmalloc-minimal-thread test/$(THREADTEST_FILE) -lpthread -ltcmalloc_minimal

# OpenBSD
test/test-obsd: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic ~/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/test-obsd test/$(TEST_FILE)

test/test-obsd-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic ~/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/test-obsd-thread test/$(THREADTEST_FILE) -lpthread

# DieHarder
test/test-dieharder: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic ../Memoryallocators/DieHard/src/libdieharder.so -o test/test-dieharder test/$(TEST_FILE)

test/test-dieharder-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic ../Memoryallocators/DieHard/src/libdieharder.so -o test/test-dieharder-thread test/$(THREADTEST_FILE) -lpthread

# Libc228
test/test-libc228: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic ../Memoryallocators/libc-2.28/libmalloc.so -o test/test-libc228 test/$(TEST_FILE)

test/test-libc228-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic ../Memoryallocators/libc-2.28/libmalloc.so -o test/test-libc228-thread test/$(THREADTEST_FILE) -pthread

test/test-libc228-thread-origin: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ../Memoryallocators/libc-2.28/libmalloc.so -o test/test-libc228-thread-origin test/$(THREADTEST_FILE) -pthread

# Libc221
test/test-libc221: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic ../Memoryallocators/libc-2.21/libmalloc.so -o test/test-libc221 test/$(TEST_FILE)

test/test-libc221-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic ../Memoryallocators/libc-2.21/libmalloc.so -o test/test-libc221-thread test/$(THREADTEST_FILE) -pthread


# Hoard
test/test-hoard: libmallocprof.so test/$(TEST_FILE)
	$(CXX) $(CFLAGS)  -rdynamic ./libmallocprof.so -rdynamic ../Memoryallocators/Hoard-spinlock-modified/src/libhoard.so -o test/test-hoard test/$(TEST_FILE)

test/test-hoard-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic ../Memoryallocators/Hoard-spinlock-modidied/src/libhoard.so -o test/test-hoard-thread test/$(THREADTEST_FILE) -pthread

# JeMalloc
test/test-jemalloc-thread: libmallocprof.so test/$(THREADTEST_FILE)
	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -o test/test-jemalloc-thread test/$(THREADTEST_FILE) -ljemalloc -pthread

# Clean
test/test-clean: test/$(TEST_FILE)
	$(CXX) $(CFLAGS) -o test/test-clean test/$(TEST_FILE)

clean:
	rm -f $(TARGETS)
	rm -f test/test-*


