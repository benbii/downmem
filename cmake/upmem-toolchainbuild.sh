#!/bin/bash
set -e
echo Usage: "$0" installPath llvmSrcPath optionalClang
MYDIR="$(dirname "$(realpath "$0")")"
mkdir -p "$1"
cd "$2"

git reset --hard HEAD
git clean -fd
git checkout llvmorg-15.0.7
zstd -d "$MYDIR/upmem-llvm.patch.zst"
git apply "$MYDIR/upmem-llvm.patch"
rm "$MYDIR/upmem-llvm.patch"

# Need clang to compile clang's various components :)
C=$3
rm -r build || true
if ! which "$3"; then
  # Our patch should make llvm15 compile under most compilers
  cmake -GNinja -S llvm -B build "-DCMAKE_INSTALL_PREFIX=$1/scratch" \
    -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON \
    -DLLVM_PARALLEL_LINK_JOBS=5 \
    -DCMAKE_C_FLAGS='-march=native -pipe' \
    -DCMAKE_CXX_FLAGS='-march=native -pipe' \
    -DLLVM_TARGETS_TO_BUILD='X86' \
    -DLLVM_ENABLE_PROJECTS='clang;lld' \
    -DLLVM_HOST_TRIPLE=x86_64-pc-linux-gnu -DCMAKE_BUILD_TYPE=Release
  ninja -C build "-j$(nproc)" install
  rm -r build
  C="$1/scratch/bin/clang"
fi

cmake -GNinja -S llvm -B build "-DCMAKE_INSTALL_PREFIX=$1" \
  -DCMAKE_C_COMPILER="$C" -DCMAKE_CXX_COMPILER="$C++" \
  -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON \
  -DLLVM_ENABLE_LLD=ON -DLLVM_ENABLE_LTO=Thin \
  -DLLVM_PARALLEL_LINK_JOBS=5 \
  -DCMAKE_C_FLAGS='-march=native -pipe' \
  -DCMAKE_CXX_FLAGS='-march=native -pipe' \
  -DLLVM_TARGETS_TO_BUILD='X86;DPU' \
  -DLLVM_ENABLE_PROJECTS='clang;lld;mlir;clang-tools-extra;openmp;compiler-rt' \
  -DCOMPILER_RT_SUPPORTED_ARCH="x86_64" \
  -DLLVM_ENABLE_PLUGINS=ON -DLLVM_ENABLE_FFI=yes \
  -DLLVM_ENABLE_LIBEDIT=yes -DLLVM_ENABLE_LIBXML2=yes \
  -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_RTTI=ON \
  -DLLVM_ENABLE_ZLIB=ON -DLLVM_ENABLE_ZSTD=ON \
  -DLLVM_HOST_TRIPLE=x86_64-pc-linux-gnu -DLLVM_INSTALL_UTILS=ON \
  -DCMAKE_BUILD_TYPE=Release
ninja -C build "-j$(nproc)" install
rm -r "$1/scratch" || true

cd "$MYDIR/.."
if ! tar xf cmake/dpu-rt.tar.zst; then
  zstd -d cmake/dpu-rt.tar.zst
  tar xf cmake/dpu-rt.tar
  rm cmake/dpu-rt.tar
fi
cmake -GNinja -DCMAKE_BUILD_TYPE=Release -Sdpu-rt -Bdpu-rt/build \
  "-DCMAKE_C_COMPILER=$1/bin/clang" "-DCMAKE_INSTALL_PREFIX=$1" 
ninja -C dpu-rt/build "-j$(nproc)" install
rm -r dpu-rt build || true
bash runTests.sh "$1"

