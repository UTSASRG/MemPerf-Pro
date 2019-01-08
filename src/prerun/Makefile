CXX = g++ 
CFLAGS += -O0 -Wall -DNDEBUG --std=c++11 -g -fPIC -I$(ROOT)/include -fno-omit-frame-pointer -Wl,--no-as-needed

MP_SRC = ../

SRCS =   	libmallochelp.cpp		\
			$(MP_SRC)real.cpp		\
			$(MP_SRC)selfmap.cpp

#DEPS = $(SRCS) $(INCS)
DEPS = $(SRCS)

INCLUDE_DIRS = -I. -I/usr/include/x86_64-linux-gnu/c++/4.8/ -I$(MP_SRC)

TARGETS =	libmallochelp.so	\
			prerun-xlibc		\
			prerun-diehard		\
			prerun-obsd			\
			prerun-freeguard	\
			prerun-tcmalloc		\
			prerun-jemalloc

LIBRARY = ./libmallochelp.so
RUNFILE = runlib.cpp

XLIBC = $(HOME)/libc-2.24/libmalloc.so
FREEGUARD = $(HOME)/FreeGuard/libfreeguard.so
OBSD = $(HOME)/Memoryallocators/OpenBSD-6.0/libomalloc.so
DIEHARD = $(HOME)/Memoryallocators/DieHard-old/src/libdieharder.so
HOARD = $(HOME)/Memoryallocators/Hoard/src/libhoard.so

all: $(TARGETS)

#libmallocprof helper library
libmallochelp.so: $(DEPS)
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmallochelp.so -ldl -lpthread

prerun-xlibc: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(XLIBC) $(RUNFILE) -o prerun-xlibc 

prerun-diehard: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(DIEHARD) $(RUNFILE) -o prerun-diehard

prerun-obsd: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(OBSD) $(RUNFILE) -o prerun-obsd
	
prerun-freeguard: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(FREEGUARD) $(RUNFILE) -o prerun-freeguard

prerun-tcmalloc: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -ltcmalloc $(RUNFILE) -o prerun-tcmalloc 

prerun-jemalloc: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -ljemalloc $(RUNFILE) -o prerun-jemalloc 

prerun-hoard: libhoard.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -lhoard $(RUNFILE) -o prerun-hoard 

.PHONY: clean
clean:
	rm -f libmallochelp.so
	rm -f $(TARGETS)
