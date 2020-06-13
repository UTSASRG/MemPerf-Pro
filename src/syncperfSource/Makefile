 CC = gcc
CFLAGS= -g -O2 -fno-omit-frame-pointer -I. -D_GNU_SOURCE -fPIC

SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c,%.o,$(SRCS)) lowlevellock.o

all: $(OBJS)

%.o : %.c
	$(CC) $(CFLAGS) -c $<
%.o : %.cpp
	$(CC) $(CFLAGS) -fpermissive -c $<
%.o : %.S
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f liblockperf.so *.o
