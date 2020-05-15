#!/bin/bash

# Hackish script to run CPU2006 with AddressSanitizer.
# Make sure to use spec version 1.2 (SPEC_CPU2006v1.2).
# Run this script like this:
# $ krun TAG [test|train|ref] benchmarks
# TAG is any word. If you use different TAGS you can runs several builds in
# parallel.
# test is a small data set, train is medium, ref is large.
# To run all C use all_c, for C++ use all_cpp

name=$1
shift
size=$1
shift

ENABLE_ASAN=${ENABLE_ASAN:-0}
ASAN_OPT=${ASAN_OPT:-0}
FASAN=
#-faddress-sanitizer

if [ "$ENABLE_ASAN" == "0" ]; then
  FASAN=""
fi

# Ignore file for known  bugs in spec.
cat <<EOF > asan_spec.ignore
fun:Perl_sv_setpvn
fun:SATD
fun:biari_init_context
EOF

rm -rf config/$name.*

CALL="-mllvm -asan-use-call=${ASAN_CALL:-0}"
STACK="-mllvm -asan-stack=${ASAN_STACK:-0}"
IGNORE="-mllvm -asan-blacklist=`pwd`/asan_spec.ignore"
OPT="-mllvm -asan-opt=$ASAN_OPT"
ENABLE_BBPLACEMENT=${ENABLE_BBPLACEMENT:-0}
ASAN_READS=${ASAN_READS:-0}
ASAN_WRITES=${ASAN_WRITES:-0}
READS="-mllvm -asan-instrument-reads=$ASAN_READS"
WRITES="-mllvm -asan-instrument-writes=$ASAN_WRITES"

ASAN_SCALE=${ASAN_SCALE:-3}
ASAN_UAR=${ASAN_UAR:-0}
ASAN_BTS=${ASAN_BTS:-0}
ASAN_PIE=${ASAN_PIE:-0}
SCALE="-mllvm -asan-mapping-scale=$ASAN_SCALE"
UAR="-mllvm -asan-use-after-return=$ASAN_UAR"
BTS="-mllvm -asan-use-bts=$ASAN_BTS"
#CLANG=${CLANG:-clang}
CLANG=/nfs/cm/scratch1/tonyliu/grace/branches/llvm/llvm/bin/clang
ATEXIT=`pwd`/atexit_print_proc_self_status.so
PIE=
BIT=${BIT:-64}

ASAN_ZERO_OFFSET=${ASAN_ZERO_OFFSET:-0}
if [ "$ASAN_ZERO_OFFSET" == "1" ]; then
  ASAN_OFFSET=0
fi

ASAN_OFFSET=${ASAN_OFFSET:--1}
if [ "$ASAN_OFFSET" == "0" ]; then
  ASAN_PIE=1
fi
OFFSET="-mllvm -asan-mapping-offset-log=$ASAN_OFFSET"

if [ "$ASAN_PIE" == "1" ]; then
  PIE="-fPIE -pie"
fi

[[ "$ENABLE_BBPLACEMENT" == "1" ]] && BBPLACEMENT="-mllvm -enable-block-placement"

COMPILER=${COMPILER:-clang}
OPT_LEVEL=${OPT_LEVEL:-"-O2"}

if [ "$COMPILER" == "clang" ] ; then
  ALL_FLAGS="-g -m$BIT -I/usr/include/x86_64-linux-gnu/c++/4.8/"
#  ALL_FLAGS="$FASAN -g $PIE $STACK $IGNORE $OPT $READS $WRITES $ASAN_EXTRA $SCALE $OFFSET $UAR $CALL -m$BIT $BBPLACEMENT"
  CC="$CLANG    -std=gnu89 $ALL_FLAGS"
  CXX="${CLANG}++ $ALL_FLAGS"
  EXTRA_LIBS=
elif [ "$COMPILER" == "gcc" ]; then
  GCC_BIN=$HOME/gcc_asan/bin
  ALL_FLAGS="$FASAN -g"
  CC="$GCC_BIN/gcc  $ALL_FLAGS"
  CXX="$GCC_BIN/g++ $ALL_FLAGS"
  EXTRA_LIBS="-Wl,-rpath=<asan lib>"
  if [ "$ENABLE_ASAN" == "1" ]; then
    EXTRA_LIBS="$EXTRA_LIBS  <asan lib> -lpthread -lstdc++ -ldl -lm"
  fi
else
  echo unknown COMPILER
  exit
fi

cat << EOF > config/$name.cfg
#monitor_wrapper = env LD_PRELOAD=$ATEXIT \$command
ignore_errors = yes
tune          = base
ext           = $name
output_format = csv, Screen
reportable    = 1
runlist=bzip2, gcc, mcf, gobmk, hmmer, sjeng, libquantum, h264ref, omnetpp, astar, xalancbmk, 433.milc, 444.namd, 447.dealII, 450.soplex, 453.povray, 470.lbm, 482.sphinx3
#runlist=perlbench, bzip2, gcc, mcf, gobmk, hmmer, sjeng, libquantum, h264ref, omnetpp, astar, xalancbmk, 433.milc, 444.namd, 447.dealII, 450.soplex, 453.povray, 470.lbm, 482.sphinx3
teeout        = yes
teerunout     = yes
hw_avail = Dec-9999
license_num = 9999
prepared_by =
tester      =
test_date = Dec-9999
strict_rundir_verify = 0

default=default=default=default:
#####################################################################
CC  = $CC -rdynamic /nfs/cm/scratch1/tonyliu/grace/branches/DoubleTake/DoubleTakeCharlie/DoubleTake-0624/libdoubletake.so
#CC  = $CC -rdynamic /nfs/cm/scratch1/tonyliu/grace/branches/DoubleTake/DoubleTakeCharlie/DoubleTake/libdoubletake.so
CXX = $CXX -rdynamic /nfs/cm/scratch1/tonyliu/grace/branches/DoubleTake/DoubleTakeCharlie/DoubleTake-0624/libdoubletake.so
#CXX = $CXX -rdynamic /nfs/cm/scratch1/tonyliu/grace/branches/DoubleTake/DoubleTakeCharlie/DoubleTake/libdoubletake.so
EXTRA_LIBS = $EXTRA_LIBS
FC         = echo

#LIBS = -L/nfs/cm/scratch1/tonyliu/grace/branches/DoubleTake/ -ldoubletake64

env_vars=1
default=default=default=default:
#ENV_LD_LIBRARY_PATH=/nfs/cm/scratch1/tonyliu/grace/branches/DoubleTake/

#####################################################################
## Base is low opt
default=base=default=default:
COPTIMIZE     = $OPT_LEVEL
CXXOPTIMIZE  =  $OPT_LEVEL
FOPTIMIZE    = -O2

#####################################################################
# 32/64 bit Portability Flags - all
#####################################################################

default=base=default=default:
PORTABILITY = -DSPEC_CPU_LP64

#####################################################################
# Portability Flags - INT
#####################################################################

400.perlbench=default=default=default:
CPORTABILITY= -DSPEC_CPU_LINUX_X64

462.libquantum=default=default=default:
CPORTABILITY= -DSPEC_CPU_LINUX

483.xalancbmk=default=default=default:
CXXPORTABILITY= -DSPEC_CPU_LINUX -include string.h

447.dealII=default=default=default:
CXXPORTABILITY= -include string.h -include stdlib.h -include cstddef

EOF

export ASAN_OPTIONS="malloc_context_size=0 redzone=32 delay_queue_size=10000 mt=0 $ASAN_OPTIONS"
. shrc
#runspec -c $name -a run -I -l --size $size -n 1 $@
runspec -c $name -a run -I -l --size $size -n 3 $@

