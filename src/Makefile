SRCS =  libheapperf.cpp 

INCS =  xmapping.hh     \
        xdefines.hh     \
        xthread.hh      \
        xheap.hh        \
        xoneheap.hh     \
        xpheap.hh     \
	      xmemory.hh      \
	      objectheader.hh \
	      mm.hh           

DEPS = $(SRCS) $(INCS)

#CXX = g++ 
 CXX = clang++ 
#-D_GNU_SOURCE 

# -march=core2 -msse3 -DSSE_SUPPORT
# When we evaluate performance, there is no need to rollback.
#-DEVALUATING_PERF

CFLAGS +=-O2 -Wall -DNDEBUG --std=c++11 -g -fPIC -I$(ROOT)/include -I$(ROOT)/heaplayers -fno-omit-frame-pointer -Werror -DDETECT_OVERFLOW -DDETECT_USAGE_AFTER_FREE -DDEBUG_LEVEL=1
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

INCLUDE_DIRS = -I. -I./heaplayers -I./heaplayers/util -I/usr/include/x86_64-linux-gnu/c++/4.8/
LIBS     := dl pthread

TARGETS = libheapperf.so

all: $(TARGETS)

#gcc -fPIC -std=c99 -c pthread_create.c
libheapperf.so: $(DEPS)
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libheapperf.so -ldl -lpthread

clean:
	rm -f $(TARGETS)


