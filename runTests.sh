#!/bin/bash
set -xe
cd "$(dirname "$0")"

cmake -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release \
  -S. -Bbuild -DCMAKE_C_COMPILER=clang
cmake --build build
if ! test -z "$1"; then
  mkdir -p devApp/{bins,objdumps}
  source ~/.local/upmem-2024.2.0-Linux-x86_64/upmem_env.sh
  for S in BS GEMV HST-L HST-S MLP OPDEMO RED SCAN-RSS SCAN-SSA SEL TRNS TS UNI VA; do
    dpu-upmem-dpurte-clang -O3 devApp/$S.c -DNR_TASKLETS=16 -o devApp/bins/$S.ummbin
    llvm-objdump -t -d devApp/bins/$S.ummbin >devApp/objdumps/$S.objdump
    llvm-objdump -s -j .atomic -j .data -j .data.__sys_host -j .data.stacks -j .mram \
      devApp/bins/$S.ummbin >>devApp/objdumps/$S.objdump
  done
fi

# if numactl --cpubind 1 true 2>/dev/null; then
#   time numactl -m0 -N0 build/dmmOpdemo 131072 256 devApp/objdumps/OPDEMO.objdump 4 &
#   time numactl -m1 -N1 build/dmmBs 5242880 640 devApp/objdumps/BS.objdump
#   time numactl -m1 -N1 build/dmmGemv 8192 2048 devApp/objdumps/GEMV.objdump
#   wait
#   time numactl -m0 -N0 build/dmmTrns 2000 200 devApp/objdumps/TRNS.objdump &
#   time numactl -m1 -N1 build/dmmHstl 2621440 1280 devApp/objdumps/HST-L.objdump
#   time numactl -m1 -N1 build/dmmHsts 2621440 1280 devApp/objdumps/HST-S.objdump
#   time numactl -m1 -N1 build/dmmMlp 1024 1024 devApp/objdumps/MLP.objdump
#   time numactl -m1 -N1 build/dmmRed 5242880 2560 devApp/objdumps/RED.objdump
#   time numactl -m1 -N1 build/dmmScanssa 3276800 1600 devApp/objdumps/SCAN-SSA.objdump
#   time numactl -m1 -N1 build/dmmScanrss 3276800 1600 devApp/objdumps/SCAN-RSS.objdump
#   time numactl -m1 -N1 build/dmmSel 5242880 2560 devApp/objdumps/SEL.objdump
#   time numactl -m1 -N1 build/dmmTs 327680 320 devApp/objdumps/TS.objdump
#   time numactl -m1 -N1 build/dmmUni 100000 256 devApp/objdumps/UNI.objdump
#   time numactl -m1 -N1 build/dmmVa 5242880 2560 devApp/objdumps/VA.objdump
#   wait
#
# else
  time build/dmmBs 5242880 640 devApp/objdumps/BS.objdump
  time build/dmmGemv 8192 2048 devApp/objdumps/GEMV.objdump
  time build/dmmHstl 2621440 1280 devApp/objdumps/HST-L.objdump
  time build/dmmHsts 2621440 1280 devApp/objdumps/HST-S.objdump
  time build/dmmMlp 1024 1024 devApp/objdumps/MLP.objdump
  time build/dmmRed 5242880 2560 devApp/objdumps/RED.objdump
  time build/dmmScanssa 3276800 1600 devApp/objdumps/SCAN-SSA.objdump
  time build/dmmScanrss 3276800 1600 devApp/objdumps/SCAN-RSS.objdump
  time build/dmmSel 5242880 2560 devApp/objdumps/SEL.objdump
  time build/dmmTrns 2000 200 devApp/objdumps/TRNS.objdump
  time build/dmmTs 327680 320 devApp/objdumps/TS.objdump
  time build/dmmUni 100000 256 devApp/objdumps/UNI.objdump
  time build/dmmVa 5242880 2560 devApp/objdumps/VA.objdump
  time build/dmmOpdemo 131072 256 devApp/objdumps/OPDEMO.objdump 4
# fi
