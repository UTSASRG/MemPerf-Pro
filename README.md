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

Todo lists: 
1. Remove the 3-level to 1-level (Page) 
2. No differentiation of passive and positive false sharing 
3. We can differentiate false sharing from true shairng. For false sharing, we can know whether this is caused by allocator or not. The starting address of an object will affect the false effect (performance). Please refer to Predator. 
4. For cache level, using the hash table. Maybe we don't need to care about the false positives caused by the accumulated effect of multiple allocations. 
5. Object table (hash table), have lots of conflict. You need to confirm whether this is the bottleneck. If it is bottleneck, check the corresponding implementation. 
