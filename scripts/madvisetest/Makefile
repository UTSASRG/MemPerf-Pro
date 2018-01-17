SRCS = real.cpp							\
			 xthread.cpp					\
			 libmadvisetest.cpp			\

INCS = log.hh				\
		list.hh								\
		mm.hh									\
		real.hh								\
		hashfuncs.hh					\
		hashheapallocator.hh	\
		hashmap.hh						\
		threadstruct.hh				\
		xdefines.hh						\
		xthread.hh

DEPS = $(SRCS) $(INCS)

CXX = clang++ 

CFLAGS = -O2 -Wall --std=c++11 -g -fno-omit-frame-pointer -DCUSTOMIZED_STACK
ifdef RDTSC
CFLAGS += -DRDTSC
endif
ifdef USE_TIMER
CFLAGS += -DUSE_TIMER
endif

INCLUDE_DIRS = -I. -I/usr/include/x86_64-linux-gnu/c++/4.8/
LIBS     := dl pthread

TARGETS = libmadvisetest.so test

all: $(TARGETS)

libmadvisetest.so: $(DEPS)
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmadvisetest.so -lsyscall_intercept -ldl -lpthread -lrt

test: test.c libmadvisetest.so
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) test.c -o test -rdynamic ./libmadvisetest.so

clean:
	rm -f $(TARGETS)
