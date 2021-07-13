# MemPerf: A General Profiler for Memory Allocators

## Sorry for the Inconvience!
To use MemPerf, you need to configure several parts in the codes to let it runnable. I haven't built a easy-run interface since I may still update the codes for some new ideas to chase a paper acception, and you can find massive debugging lines inside the codes. I will definitely make it easy to use, clean, and completed after I get the paper accepted.

## So How to Use MemPerf Right Now?
Currently the src/ folder is stale and discarded, you should use the codes in src_light/. Before doing compilaton, you need to check these:
1. The definitions in definevalues.h. There are many things that need confirm: enabled functionalities, the perf event hex numbers, the sampling period, and all kinds of max values. Make sure the values fit your own environment.
2. You need to specify the input .info file in the function getInputInfoFileName() in programstatus.cpp. Info files are located in the folder info/. If you are using an allocator that doesn't fit any of those, you need to write one by yourself. Also, you need to check the output path in the function openOutputFile() in the same file.
3. Change the values in the array addressRanges in shadowmemory.cpp and the definition NUM_ADDRESS_RANGE in shadowmemory.h. They are the address ranges of the heap objects, which depend on your running environment and the allocator.
After all the changes in the codes, you can re-compile the source codes by make. Currently we also have to re-compile the application:
```
g++ -O2 -pipe -g -g3 -ggdb3 -Wall -DNDEBUG --std=c++11 -fPIC -I$(ROOT)/include -Wno-unused-result -fno-omit-frame-pointer -Wl,--no-as-needed -lrt -rdynamic ./libmallocprof.so -rdynamic $(ALLOCATOR_PATH) -o test/$(TEST_BINARY) test/$(TEST_FILE
```


