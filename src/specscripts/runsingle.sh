#!/bin/sh

cd /nfs/cm/scratch1/tonyliu/grace/branches/spec2k6/benchspec/CPU2006/403.gcc/run/run_base_test_test.0000 
./gcc_base.test cccp.in -o cccp.s
cd -
#/nfs/cm/scratch1/tonyliu/grace/branches/spec2k6/benchspec/CPU2006/400.perlbench/run/run_base_test_orig.0000/perlbench_base.orig 
#/nfs/cm/scratch1/tonyliu/grace/branches/spec2k6/benchspec/CPU2006/471.omnetpp/run/run_base_ref_none.0015/omnetpp_base.none ./omnetpp.ini 
#LD_LIBRARY_PATH=/nfs/cm/scratch1/tonyliu/grace/branches/stopgap-singlethread LD_PRELOAD=/nfs/cm/scratch1/tonyliu/grace/branches/stopgap-singlethread/libstopgap64.so /nfs/cm/scratch1/tonyliu/grace/branches/spec2k6/benchspec/CPU2006/429.mcf/run/run_base_ref_none.0012/mcf_base.none ./inp.in 
#LD_LIBRARY_PATH=/nfs/cm/scratch1/tonyliu/grace/branches/stopgap-singlethread LD_PRELOAD=/nfs/cm/scratch1/tonyliu/grace/branches/stopgap-singlethread/libstopgap64.so /nfs/cm/scratch1/tonyliu/grace/branches/spec2k6/benchspec/CPU2006/473.astar/run/run_base_ref_none.0001/astar_base.none ./BigLakes2048.cfg 
#LD_LIBRARY_PATH=/nfs/cm/scratch1/tonyliu/grace/branches/stopgap-singlethread LD_PRELOAD=/nfs/cm/scratch1/tonyliu/grace/branches/stopgap-singlethread/libstopgap64.so /nfs/cm/scratch1/tonyliu/grace/branches/spec2k6/benchspec/CPU2006/453.povray/run/run_base_ref_none.0027/povray_base.none ./SPEC-benchmark-ref.ini 
