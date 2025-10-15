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
time build/dmmCOMPACT 15728640 2560 build/devApp/objdumps/COMPACT.objdump
time build/dmmHST 7864320 1280 build/devApp/objdumps/HST.objdump
time build/dmmGEMV 10240 2048 build/devApp/objdumps/GEMV.objdump
time build/dmmMLP 1024 1024 build/devApp/objdumps/MLP.objdump
time build/dmmNW 2000 1000 64 build/devApp/objdumps/NW.objdump
time build/dmmOPDEMO 262144 512 build/devApp/objdumps/OPDEMO.objdump 3
time build/dmmOPDEMOF 262144 512 build/devApp/objdumps/OPDEMOF.objdump 4
time build/dmmRED 96000000 1600 build/devApp/objdumps/RED.objdump
time build/dmmSCAN 96000000 1600 build/devApp/objdumps/SCAN.objdump
time build/dmmSPMV 44444 888 build/devApp/objdumps/SPMV.objdump
time build/dmmTRNS 2000 200 build/devApp/objdumps/TRNS.objdump
time build/dmmTS 655360 640 build/devApp/objdumps/TS.objdump
time build/dmmUNI 100000 512 build/devApp/objdumps/UNI.objdump
time build/dmmVA 15728640 2560 build/devApp/objdumps/VA.objdump

time build/dmmBS 5242880 640 build/devApp/rvbins/BS
time build/dmmCOMPACT 15728640 2560 build/devApp/rvbins/COMPACT
time build/dmmHST 7864320 1280 build/devApp/rvbins/HST
time build/dmmGEMV 10240 2048 build/devApp/rvbins/GEMV
time build/dmmMLP 1024 1024 build/devApp/rvbins/MLP
time build/dmmNW 2000 1000 64 build/devApp/rvbins/NW
time build/dmmOPDEMO 262144 512 build/devApp/rvbins/OPDEMO 3
time build/dmmOPDEMOF 262144 512 build/devApp/rvbins/OPDEMOF 4
time build/dmmRED 96000000 1600 build/devApp/rvbins/RED
time build/dmmSCAN 96000000 1600 build/devApp/rvbins/SCAN
time build/dmmSPMV 44444 888 build/devApp/rvbins/SPMV
time build/dmmTRNS 2000 200 build/devApp/rvbins/TRNS
time build/dmmTS 655360 640 build/devApp/rvbins/TS
time build/dmmUNI 100000 512 build/devApp/rvbins/UNI
time build/dmmVA 15728640 2560 build/devApp/rvbins/VA

if ! [ -f hostApp/BFS/csr.txt ]; then
  wget -O hostApp/BFS/csr.txt.zst \
    "https://drive.usercontent.google.com/download?id=1bXYWq_4dXrJcst5jsLL3CJTeZTQCrBlr&export=download"
  zstd -d hostApp/BFS/csr.txt.zst
fi
build/dmmBFS simpleBFSCpu 0 hostApp/BFS/csr.txt /tmp/dmmBfsCOut >/dev/null

time build/dmmBFS simpleBFSDpu 0 hostApp/BFS/csr.txt /tmp/dmmBfsDOut \
  build/devApp/objdumps/BFS.objdump 192 
diff /tmp/dmmBfs{C,D}Out # Check the output is indeed correct here.
time build/dmmBFS simpleBFSDpu 0 hostApp/BFS/csr.txt /tmp/dmmBfsDOut \
  build/devApp/rvbins/BFS 192
diff /tmp/dmmBfs{C,D}Out # Another check for umm rv
rm /tmp/dmmBfs{C,D}Out
