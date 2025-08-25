#!/bin/bash
set -xe
cd "$(dirname "$0")"

if test -n "$1"; then
  mkdir -p build devApp/rvbins
  cmake -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release \
    -S. -Bbuild "-DCMAKE_C_COMPILER=$1/bin/clang" -DDMM_ISA=riscv
  cmake --build build
  # Vanilla llvm does not support binning strategies making RF hazard intensity
  # basically random, and in O2 these cases are unfortunate enough to underperform
  # Og. Thankfully -flto is almost always beneficial
  for S in NW SCAN TS; do
    "$1/bin/clang" --target=riscv32 -DNR_TASKLETS=16 -mcpu=umm -Og -flto \
      devApp/$S.c -o devApp/rvbins/$S -fno-builtin
  done
  for S in BFS BS COMPACT GEMV HST MLP OPDEMO OPDEMOF RED SPMV TRNS UNI VA; do
    "$1/bin/clang" --target=riscv32 -DNR_TASKLETS=16 -mcpu=umm -O3 -flto \
      devApp/$S.c -o devApp/rvbins/$S -fno-builtin &
  done
fi

if ! [ -f hostApp/BFS/csr.txt ]; then
  wget "https://drive.usercontent.google.com/download?id=1bXYWq_4dXrJcst5jsLL3CJTeZTQCrBlr&export=download&authuser=0"
  zstd -d hostApp/BFS/csr.txt.zst
fi
wait
time build/dmmBfs simpleBFSDpu 0 hostApp/BFS/csr.txt /tmp/dmmBfsDOut devApp/rvbins/BFS 192
build/dmmBfs simpleBFSCpu 0 hostApp/BFS/csr.txt /tmp/dmmBfsCOut >/dev/null
# Check the output is indeed correct here.
diff /tmp/dmmBfs{C,D}Out
rm /tmp/dmmBfs{C,D}Out

# time build/dmmVaSimple 131072 256 devApp/rvbins/VA-SIMPLE
time build/dmmBs 5242880 640 devApp/rvbins/BS
time build/dmmCompact 5242880 2560 devApp/rvbins/COMPACT
time build/dmmHst 2621440 1280 devApp/rvbins/HST
time build/dmmGemv 8192 2048 devApp/rvbins/GEMV
time build/dmmMlp 1024 1024 devApp/rvbins/MLP
time build/dmmNw 1500 1000 64 devApp/rvbins/NW
time build/dmmOpdemo 131072 256 devApp/rvbins/OPDEMO 3
time build/dmmOpdemof 131072 256 devApp/rvbins/OPDEMOF 3
time build/dmmRed 3276800 1600 devApp/rvbins/RED
time build/dmmScan 3276800 1600 devApp/rvbins/SCAN
time build/dmmSpmv 9999 666 devApp/rvbins/SPMV
time build/dmmTrns 2000 200 devApp/rvbins/TRNS
time build/dmmTs 327680 320 devApp/rvbins/TS
time build/dmmUni 100000 256 devApp/rvbins/UNI
time build/dmmVa 5242880 2560 devApp/rvbins/VA
