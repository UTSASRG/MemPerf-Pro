SRCS = real.cpp							\
			 xthread.cpp					\
			 libmadvisetest.cpp			\

INCS = log.hh				\
		list.hh								\
		mm.hh									\
		real.hh								\
		slist.h               \
		hashfuncs.hh					\
		hashheapallocator.hh	\
		hashmap.hh						\
		threadstruct.hh				\
		xdefines.hh						\
		xthread.hh

DEPS = $(SRCS) $(INCS)

CXX = clang++ 

CFLAGS = -O2 -Wall --std=c++11 -g -fno-omit-frame-pointer -DCUSTOMIZED_STACK

INCLUDE_DIRS = -I. -I/usr/include/x86_64-linux-gnu/c++/4.8/ -I/home/sam/libsyscall_intercept/include
LIBS     := dl pthread

TARGETS = libmadvisetest.so

all: $(TARGETS)

libmadvisetest.so: $(DEPS)
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmadvisetest.so -lsyscall_intercept -ldl -lpthread -lrt

clean:
	rm -f $(TARGETS)
