#!/bin/bash
set -xe
cd "$(dirname "$0")"

if test -n "$1"; then
  rm -r build || true
  mkdir -p build devApp/{bins,objdumps}
  cmake -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release \
    -S. -Bbuild -DCMAKE_C_COMPILER=clang -DDMM_ISA=upmem
  cmake --build build

  for S in BS COMPACT GEMV HST MLP OPDEMO OPDEMOF SPMV \
      NW RED SCAN TRNS TS UNI VA VA-SIMPLE; do
    "$1/bin/clang" -O3 --target=dpu-upmem-dpurte -mcpu=v1A -g \
      devApp/$S.c -DNR_TASKLETS=16 -o devApp/bins/$S.ummbin
    "$1/bin/llvm-objdump" -t -d devApp/bins/$S.ummbin >devApp/objdumps/$S.objdump
    "$1/bin/llvm-objdump" -s -j .atomic -j .data -j .data.__sys_host -j .data.stacks \
      -j .mram devApp/bins/$S.ummbin >>devApp/objdumps/$S.objdump
  done
  # use libomp from this llvm installation
  export LD_LIBRARY_PATH="$1/lib/x86_64-pc-linux-gnu:${LD_LIBRARY_PATH}"
fi

# time build/dmmVaSimple 131072 256 devApp/objdumps/VA-SIMPLE.objdump
time build/dmmBs 5242880 640 devApp/objdumps/BS.objdump
time build/dmmCompact 5242880 2560 devApp/objdumps/COMPACT.objdump
time build/dmmHst 2621440 1280 devApp/objdumps/HST.objdump
time build/dmmGemv 8192 2048 devApp/objdumps/GEMV.objdump
time build/dmmMlp 1024 1024 devApp/objdumps/MLP.objdump
time build/dmmNw 1500 1000 64 devApp/objdumps/NW.objdump
time build/dmmOpdemo 131072 256 devApp/objdumps/OPDEMO.objdump 3
time build/dmmOpdemof 131072 256 devApp/objdumps/OPDEMOF.objdump 3
time build/dmmRed 5242880 2560 devApp/objdumps/RED.objdump
time build/dmmScan 3276800 1600 devApp/objdumps/SCAN.objdump
time build/dmmSpmv 9999 666 devApp/objdumps/SPMV.objdump
time build/dmmTrns 2000 200 devApp/objdumps/TRNS.objdump
time build/dmmTs 327680 320 devApp/objdumps/TS.objdump
time build/dmmUni 100000 256 devApp/objdumps/UNI.objdump
time build/dmmVa 5242880 2560 devApp/objdumps/VA.objdump

if ! [ -f hostApp/BFS/csr.txt ]; then
  wget -O hostApp/BFS/csr.txt.zst \
    "https://drive.usercontent.google.com/download?id=1bXYWq_4dXrJcst5jsLL3CJTeZTQCrBlr&export=download"
  zstd -d hostApp/BFS/csr.txt.zst
fi
time build/dmmBfs simpleBFSDpu 0 hostApp/BFS/csr.txt /tmp/dmmBfsDOut devApp/objdumps/BFS.objdump 192 
build/dmmBfs simpleBFSCpu 0 hostApp/BFS/csr.txt /tmp/dmmBfsCOut >/dev/null
# Check the output is indeed correct here.
diff /tmp/dmmBfs{C,D}Out
rm /tmp/dmmBfs{C,D}Out
