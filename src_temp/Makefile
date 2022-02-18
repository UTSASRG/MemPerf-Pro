
SRCS = 	\
		libmallocprof.cpp \
		allocatingstatus.cpp \
		backtrace.cpp \
		callsite.cpp \
		globalstatus.cpp \
		memoryusage.cpp \
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
		backtrace.h \
		callsite.h \
		definevalues.h \
		globalstatus.h \
		hashfuncs.hh \
		hashlist.hh \
		hashmap.hh \
		memoryusage.h \
		memsample.h \
		mymalloc.h \
		objTable.h \
		predictor.h \
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
CFLAGS2 += -Ofast -pipe -g -g3 -ggdb3 -Wall -DNDEBUG --std=c++11 -fPIC -I$(ROOT)/include -Wno-unused-result -fno-omit-frame-pointer -Wl,--no-as-needed -lrt

THREADTEST_FILE = threadTest.cpp
TEST_FILE = alloc.cpp

#CFLAGS += -DNO_PMU
#CFLAGS2 += -DNO_PMU

INCLUDE_DIRS = -I.
LIBS     := dl pthread


TARGETS = libmallocprof.so


# Libmallocprof
libmallocprof.so: $(DEPS)
	$(CXX) $(CFLAGS2) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmallocprof.so -ldl -lpthread

#test/test-tcmalloc-minimal-thread: libmallocprof.so test/$(THREADTEST_FILE)
#	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic /home/tpliu/jinzhou/allocators/libtcmalloc_minimal.so -o test/test-tcmalloc-minimal-thread test/$(THREADTEST_FILE) -lpthread
#
#test/test-libc228-thread: libmallocprof.so test/$(THREADTEST_FILE)
#	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic /home/tpliu/jinzhou/allocators/libc228.so -o test/test-libc228-thread test/$(THREADTEST_FILE) -pthread
#
#test/test-libc221-thread: libmallocprof.so test/$(THREADTEST_FILE)
#	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic /home/tpliu/jinzhou/allocators/libc221.so -o test/test-libc221-thread test/$(THREADTEST_FILE) -pthread
#
#test/test-hoard-thread: libmallocprof.so test/$(THREADTEST_FILE)
#	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic /home/tpliu/jinzhou/allocators/libhoard.so -o test/test-hoard-thread test/$(THREADTEST_FILE) -pthread
#
#test/test-jemalloc-thread: libmallocprof.so test/$(THREADTEST_FILE)
#	$(CXX) $(CFLAGS) -rdynamic ./libmallocprof.so -rdynamic /home/tpliu/jinzhou/allocators/libjemalloc.so -o test/test-jemalloc-thread test/$(THREADTEST_FILE) -pthread

clean:
	rm -f $(TARGETS)


