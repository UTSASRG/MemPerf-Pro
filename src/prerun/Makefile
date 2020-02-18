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
			prerun-jemalloc     \
			prerun-tcmalloc-minimal

LIBRARY = ./libmallochelp.so
RUNFILE = runlib.cpp

NUMALLOC_LIBC = /media/umass/datasystem/xin/numalloc/source/libnumalloc.so
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

prerun-numalloc: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(NUMALLOC_LIBC) $(RUNFILE) -o prerun-numalloc

prerun-tcmalloc: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -ltcmalloc $(RUNFILE) -o prerun-tcmalloc

prerun-tcmalloc-minimal: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -ltcmalloc_minimal $(RUNFILE) -o prerun-tcmalloc-minimal

prerun-jemalloc: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -ljemalloc $(RUNFILE) -o prerun-jemalloc 

prerun-hoard: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(HOARD) $(RUNFILE) -o prerun-hoard

#prerun-hoard: libhoard.so $(RUNFILE)
#	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -lhoard $(RUNFILE) -o prerun-hoard

.PHONY: clean
clean:
	rm -f libmallochelp.so
	rm -f $(TARGETS)
