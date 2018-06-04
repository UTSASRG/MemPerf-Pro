SRCS = malloc.c \
			 morecore.c

INCS = include/malloc.h

DEPS = $(SRCS) $(INCS)

CXX = gcc 
CFLAGS += -O2 -g -std=c99 -Wall -fPIC -fno-omit-frame-pointer
#CFLAGS += -O2 -g -std=c99 -Wall -fPIC -fno-omit-frame-pointer -DNDEBUG

CFLAGS2 = -O0 --std=c++11 -g -fno-omit-frame-pointer

INCLUDE_DIRS = -I. -I./include/ -I./include/sysdeps/x86_64/ -I./include/sysdeps/x86_64/nptl/ -I./include/sysdeps/generic/ -I./include/sysdeps/unix/sysv/linux/x86_64/ -I./include/sysdeps/unix/sysv/linux/ -I./include/sysdeps/x86
LIBS     := dl pthread

TARGETS = libmalloc.so test/test

all: $(TARGETS)

libmalloc.so: $(DEPS)
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmalloc.so -ldl -lpthread -lrt

test/test: libmalloc.so test/alloc.cpp
	$(CXX) $(CFLAGS2) -rdynamic ./libmalloc.so -o test/test test/alloc.cpp

clean:
	rm -f $(TARGETS)
