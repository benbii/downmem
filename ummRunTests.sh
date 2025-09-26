#!/bin/bash
set -xe
cd "$(dirname "$0")"

if test -n "$1"; then
  rm -r build || true
  mkdir -p build
  cmake -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release \
    -S. -Bbuild -DCMAKE_C_COMPILER="$1/bin/clang" -DDMM_UPMEM=ON -DDMM_RV=ON
  ninja -C build all dpuExamples
  # use libomp from this llvm installation
  export LD_LIBRARY_PATH="$1/lib/x86_64-pc-linux-gnu:${LD_LIBRARY_PATH}"
fi

time build/dmmBS 5242880 640 build/devApp/objdumps/BS.objdump
time build/dmmCOMPACT 5242880 2560 build/devApp/objdumps/COMPACT.objdump
time build/dmmHST 2621440 1280 build/devApp/objdumps/HST.objdump
time build/dmmGEMV 8192 2048 build/devApp/objdumps/GEMV.objdump
time build/dmmMLP 1024 1024 build/devApp/objdumps/MLP.objdump
time build/dmmNW 1500 1000 64 build/devApp/objdumps/NW.objdump
time build/dmmOPDEMO 131072 256 build/devApp/objdumps/OPDEMO.objdump 3
time build/dmmOPDEMOF 131072 256 build/devApp/objdumps/OPDEMOF.objdump 3
time build/dmmRED 5242880 2560 build/devApp/objdumps/RED.objdump
time build/dmmSCAN 3276800 1600 build/devApp/objdumps/SCAN.objdump
time build/dmmSPMV 9999 666 build/devApp/objdumps/SPMV.objdump
time build/dmmTRNS 2000 200 build/devApp/objdumps/TRNS.objdump
time build/dmmTS 327680 320 build/devApp/objdumps/TS.objdump
time build/dmmUNI 100000 256 build/devApp/objdumps/UNI.objdump
time build/dmmVA 5242880 2560 build/devApp/objdumps/VA.objdump

if ! [ -f hostApp/BFS/csr.txt ]; then
  wget -O hostApp/BFS/csr.txt.zst \
    "https://drive.usercontent.google.com/download?id=1bXYWq_4dXrJcst5jsLL3CJTeZTQCrBlr&export=download"
  zstd -d hostApp/BFS/csr.txt.zst
fi
time build/dmmBFS simpleBFSDpu 0 hostApp/BFS/csr.txt /tmp/dmmBfsDOut build/devApp/objdumps/BFS.objdump 192 
build/dmmBFS simpleBFSCpu 0 hostApp/BFS/csr.txt /tmp/dmmBfsCOut >/dev/null
# Check the output is indeed correct here.
diff /tmp/dmmBfs{C,D}Out
rm /tmp/dmmBfs{C,D}Out
