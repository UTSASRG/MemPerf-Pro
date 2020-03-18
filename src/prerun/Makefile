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
			prerun-glibc221		\
			prerun-glibc224		\
			prerun-diehard		\
			prerun-obsd			\
			prerun-jemalloc     \
			prerun-tcmalloc-minimal \
			prerun-hoard

LIBRARY = ./libmallochelp.so
RUNFILE = runlib.cpp

GLIBC221 = $(HOME)/Memoryallocators/libc-2.21/libmalloc.so
GLIBC224 = $(HOME)/Memoryallocators/libc-2.24/libmalloc.so
OBSD = $(HOME)/Memoryallocators/OpenBSD-6.0/libomalloc.so
DIEHARD = $(HOME)/Memoryallocators/DieHard/src/libdieharder.so
HOARD = $(HOME)/Memoryallocators/Hoard/src/libhoard.so
TCMIN = /usr/local/lib/libtcmalloc_minimal.so
JE = $(HOME)/Memoryallocators/jemalloc/lib/libjemalloc.so

all: $(TARGETS)

#libmallocprof helper library
libmallochelp.so: $(DEPS)
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmallochelp.so -ldl -lpthread

prerun-glibc221: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(GLIBC224) $(RUNFILE) -o prerun-glibc221

prerun-glibc224: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(GLIBC224) $(RUNFILE) -o prerun-glibc224

prerun-diehard: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(DIEHARD) $(RUNFILE) -o prerun-diehard

prerun-obsd: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(OBSD) $(RUNFILE) -o prerun-obsd

prerun-tcmalloc-minimal: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(TCMIN) $(RUNFILE) -o prerun-tcmalloc-minimal
#	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -ltcmalloc_minimal $(RUNFILE) -o prerun-tcmalloc-minimal

prerun-jemalloc: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(JE) $(RUNFILE) -o prerun-jemalloc
#	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -ljemalloc $(RUNFILE) -o prerun-jemalloc

prerun-hoard: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(HOARD) $(RUNFILE) -o prerun-hoard


.PHONY: clean
clean:
	rm -f libmallochelp.so
	rm -f $(TARGETS)
