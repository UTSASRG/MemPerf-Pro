#!/bin/bash

make clean
make

threads=40

for allocator in {"tcmalloc","jemalloc"}
do
	for object_size in {9,4099}
	do
		for allocation_per_second in {1,}
		do
			/micro-$allocator $threads $object_size $allocation_per_second
		done
	done
done

