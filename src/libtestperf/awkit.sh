#!/bin/bash

/usr/bin/time -p -o out.txt $1
awk -f awkit.awk out.txt
rm out.txt
