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

TARGETS = libmemperf.so test/test test/test-clean test/test-obsd

all: $(TARGETS)

libmemperf.so: $(DEPS)
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmemperf.so -lsyscall_intercept -ldl -lpthread

test/test: libmemperf.so test/alloc.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -o test/test test/alloc.cpp

test/test-obsd: libmemperf.so test/alloc.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmemperf.so -rdynamic /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so -o test/test-obsd test/alloc.cpp

test/test-clean: test/alloc.cpp
	$(CXX) $(CFLAGS2) -o test/test-clean test/alloc.cpp

clean:
	rm -f $(TARGETS)
