#!/bin/bash
set -xe
cd "$(dirname "$0")"

# 检查并解压 upmem 文件夹
THIRD_PARTY_DIR="third-party"
UPMEM_TAR_GZ="$THIRD_PARTY_DIR/upmem-2025.1.0-Linux-x86_64.tar.gz"
UPMEM_DIR="$THIRD_PARTY_DIR/upmem"

if [ ! -d "$UPMEM_DIR" ] && [ -f "$UPMEM_TAR_GZ" ]; then
    echo "解压 upmem..."
    tar -xzf "$UPMEM_TAR_GZ" -C "$THIRD_PARTY_DIR"
    if [ -d "$THIRD_PARTY_DIR/upmem-2025.1.0-Linux-x86_64" ]; then
        mv "$THIRD_PARTY_DIR/upmem-2025.1.0-Linux-x86_64" "$UPMEM_DIR"
    fi
    echo "upmem 解压完成"
elif [ ! -d "$UPMEM_DIR" ] && [ ! -f "$UPMEM_TAR_GZ" ]; then
    echo "警告: 未找到 upmem 目录和压缩文件 $UPMEM_TAR_GZ"
fi

if test -n "$1"; then
  rm -r build || true
  mkdir -p build

  OMP_INCLUDE_PATH="/usr/lib/llvm-12/lib/clang/12.0.1/include/"  
  OMP_LIB_PATH="/usr/lib/llvm-12/lib"
  
  cmake -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release \
    -S. -Bbuild \
    -DCMAKE_C_COMPILER="$1/bin/clang" \
    -DDMM_UPMEM=ON -DDMM_RV=OFF \
    -DOpenMP_C_FLAGS="-fopenmp=libomp -I${OMP_INCLUDE_PATH}" \
    -DOpenMP_C_LIB_NAMES="libomp" \
    -DOpenMP_libomp_LIBRARY="${OMP_LIB_PATH}/libomp.so"

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
