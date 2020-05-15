#!/bin/sh

#nohup ./asan-doubletake.sh doubletake ref &> doubletake.ref.log
#nohup ./asan-stopgap.sh asan test &> stopgap.log
nohup ./asan-origin.sh asan ref &> original.log
#nohup ./asan.sh asan ref &> asan.log
