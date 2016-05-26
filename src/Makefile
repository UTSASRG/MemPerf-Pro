SRCS =  libmemperf.cpp \
				real.cpp \
				memsample.c

INCS =  xthread.hh     \
				memsample.h 	 \
				real.hh

DEPS = $(SRCS) $(INCS)

#CXX = g++ 
 CXX = clang++ 
#-D_GNU_SOURCE 

# -march=core2 -msse3 -DSSE_SUPPORT
# When we evaluate performance, there is no need to rollback.
#-DEVALUATING_PERF

CFLAGS += -O0 -Wall -DNDEBUG --std=c++11 -g -fPIC -I$(ROOT)/include -fno-omit-frame-pointer -DDETECT_OVERFLOW -DDETECT_USAGE_AFTER_FREE -DDEBUG_LEVEL=1
#-DDETECT_MEMORY_LEAKAGE -DDETECT_USAGE_AFTER_FREE
# CFLAGS += -DDETECT_OVERFLOW -DDETECT_MEMORY_LEAKAGE -DDETECT_USAGE_AFTER_FREE
# -DEVALUATING_PERF
#CFLAGS += -DDETECT_OVERFLOW -DDETECT_MEMORY_LEAKAGE -DEVALUATING_PERF
#CFLAGS   = -g -O2 -DSSE_SUPPORT -fno-omit-frame-pointer -DREPRODUCIBLE_FDS -DHANDLE_SYSCALL -DDETECT_OVERFLOW 
#CFLAGS   = -g -O2 -DSSE_SUPPORT -fno-omit-frame-pointer -DDEBUG_ROLLBACK -DHANDLE_SYSCALL -DDETECT_OVERFLOW 
#-DEVALUATING_PERF 
#CFLAGS   = -g -O2 -DSSE_SUPPORT -fno-omit-frame-pointer -DHANDLE_SYSCALL -DDETECT_OVERFLOW -DEVALUATING_PERF 
# -DDETECT_OVERFLOW 
# -DDETECT_MEMORY_LEAKAGE 
# -DEVALUATING_PERF

# Overflow
#CFLAGS   = -g -O2 -DSSE_SUPPORT -fno-omit-frame-pointer -DHANDLE_SYSCALL -DDETECT_OVERFLOW -DEVALUATING_PERF

# Memory use-after-free errors
#CFLAGS   = -g -O2 -DSSE_SUPPORT -fno-omit-frame-pointer -DHANDLE_SYSCALL -DDETECT_USAGE_AFTER_FREE -DEVALUATING_PERF

# Memory Leakage
#CFLAGS   = -g -O2 -DSSE_SUPPORT -fno-omit-frame-pointer -DHANDLE_SYSCALL -DDETECT_MEMORY_LEAKAGE -DEVALUATING_PERF

INCLUDE_DIRS = -I. -I/usr/include/x86_64-linux-gnu/c++/4.8/
LIBS     := dl pthread

TARGETS = libmemperf.so test/test

all: $(TARGETS)

libmemperf.so: $(DEPS)
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmemperf.so -ldl -lpthread

test/test: libmemperf.so test/alloc.cpp
	$(CXX) $(CFLAGS) -g -rdynamic ./libmemperf.so -o test/test test/alloc.cpp

clean:
	rm -f $(TARGETS)
