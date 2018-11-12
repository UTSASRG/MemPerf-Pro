# maprof: A General Allocator for Different Memory Allocators

Status:
About allocator information. We should also check whether the library’s location. It will be bad to put everywhere. Also, it will be impossible to run canneal, fluidanimate, freqmine, 
 
LD_PRELOAD=/home/tongpingliu/projects/mmprof/src/libmallocprof.so ./canneal-jemalloc 15 15000 2000 ../../datasets/canneal/2500000.nets 6000

With the error like this:
Failed to open allocator info file. Make sure to run the prerun lib and
the file (i.e. allocator.info) is in this directory. Quit: No such file or directory

Crashes on bodytrack, dedup, ferret, reverse_index, streamcluster
With the error:
ERROR: incremented page map entry at 0x14003f708 to size 4416 > 4096

