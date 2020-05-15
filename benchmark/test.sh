#!/bin/bash

make clean
make

threads=40
set -x
for allocator in {"tcmalloc","jemalloc"}
do
	for object_size in {9,4099}
	do
		./micro-$allocator $threads $object_size 100 1000
		./micro-$allocator $threads $object_size 1000 10000
		for allocation_per_second in {5000,10000,20000}
		do
			./micro-$allocator $threads $object_size $allocation_per_second
		done
	done
done

