CXX = g++ 
CFLAGS += -O0 -Wall -DNDEBUG --std=c++11 -g -fPIC -I$(ROOT)/include -fno-omit-frame-pointer -Wl,--no-as-needed

MP_SRC = /home/richard/MallocProf/src/

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

LIBRARY = /home/richard/MallocProf/src/prerun/libmallochelp.so
RUNFILE = runlib.cpp

XLIBC = /home/richard/Memoryallocators/libc-2.24/libmalloc.so
FREEGUARD = /home/richard/FreeGuard/libfreeguard.so
OBSD = /home/sam/Memoryallocators/OpenBSD-6.0/libomalloc.so
DIEHARD = /home/sam/Memoryallocators/DieHard-old/src/libdieharder.so
HOARD =

all: $(TARGETS)

#libmallocprof helper library
libmallochelp.so: $(DEPS)
	$(CXX) $(CFLAGS) $(INCLUDE_DIRS) -shared -fPIC $(SRCS) -o libmallochelp.so -ldl

prerun-xlibc: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(XLIBC) $(RUNFILE) -o prerun-xlibc

prerun-diehard: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(DIEHARD) $(RUNFILE) -o prerun-diehard

prerun-obsd: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(OBSD) $(RUNFILE) -o prerun-obsd
	
prerun-freeguard: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -rdynamic $(FREEGUARD) $(RUNFILE) -o prerun-freeguard

prerun-tcmalloc: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -ltcmalloc $(RUNFILE) -o prerun-tcmalloc -lpthread

prerun-jemalloc: libmallochelp.so $(RUNFILE)
	$(CXX) $(CFLAGS) -rdynamic $(LIBRARY) -ljemalloc $(RUNFILE) -o prerun-jemalloc

.PHONY: clean
clean:
	rm -f libmallochelp.so
	rm -f $(TARGETS)
