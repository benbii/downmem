#!/bin/bash
set -xe
# Cooked by bash's asshole directory management and quoting syntax
FUCK="$(dirname "$(realpath "$0")")"

cd "$2"
git checkout llvmorg-20.1.8
git am "$FUCK/rvupmem-llvm.patch"
# Need clang to compile clang's various components :)
C=$3
if ! which "$3"; then
  rm -r build || true
  cmake -GNinja -S llvm -B build "-DCMAKE_INSTALL_PREFIX=$1/scratch" \
    -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON \
    -DLLVM_PARALLEL_LINK_JOBS=20 -DLLVM_RAM_PER_LINK_JOB=5500 \
    -DCMAKE_C_FLAGS='-march=native -pipe' \
    -DCMAKE_CXX_FLAGS='-march=native -pipe' \
    -DLLVM_TARGETS_TO_BUILD='X86;RISCV' \
    -DLLVM_ENABLE_PROJECTS='clang;lld' \
    -DLLVM_HOST_TRIPLE=x86_64-pc-linux-gnu -DCMAKE_BUILD_TYPE=Release
  ninja -C build -j$(nproc) install
  rm -r build
  C="$1/scratch/bin/clang"
fi

cmake -GNinja -S llvm -B build "-DCMAKE_INSTALL_PREFIX=$1" \
  -DCMAKE_C_COMPILER="$C" -DCMAKE_CXX_COMPILER="$C++" \
  -DLLVM_ENABLE_LLD=ON -DLLVM_ENABLE_LTO=Thin \
  -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON \
  -DLLVM_PARALLEL_LINK_JOBS=20 -DLLVM_RAM_PER_LINK_JOB=5500 \
  -DCMAKE_C_FLAGS='-march=native -pipe' \
  -DCMAKE_CXX_FLAGS='-march=native -pipe' \
  -DLLVM_TARGETS_TO_BUILD='X86;RISCV' \
  -DLLVM_ENABLE_PROJECTS='clang;lld;mlir;clang-tools-extra;openmp;compiler-rt' \
  -DCOMPILER_RT_SUPPORTED_ARCH="x86_64;riscv" \
  -DLLVM_ENABLE_PLUGINS=ON -DLLVM_ENABLE_FFI=yes \
  -DLLVM_ENABLE_LIBEDIT=yes -DLLVM_ENABLE_LIBXML2=yes \
  -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_RTTI=ON -DLLVM_INSTALL_UTILS=ON \
  -DLLVM_ENABLE_ZLIB=ON -DLLVM_ENABLE_ZSTD=ON \
  -DLLVM_HOST_TRIPLE=x86_64-pc-linux-gnu -DCMAKE_BUILD_TYPE=Release
ninja -C build -j$(nproc) install
rm -r "$1/scratch" || true

mkdir -p build/crtrv32 && cd build/crtrv32
cmake -S ../../compiler-rt -B. -GNinja \
  -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_C_COMPILER=$1/bin/clang" \
  "-DCMAKE_CXX_COMPILER=$1/bin/clang++" \
  -DCMAKE_C_COMPILER_TARGET=riscv32-unknown-elf \
  -DCMAKE_CXX_COMPILER_TARGET=riscv32-unknown-elf \
  -DCMAKE_ASM_COMPILER_TARGET=riscv32-unknown-elf \
  -DCMAKE_C_FLAGS="-march=rv32im_zbb -nostdlib" \
  -DCMAKE_CXX_FLAGS="-march=rv32im_zbb -nostdlib" \
  -DCMAKE_ASM_FLAGS="-march=rv32im_zbb -nostdlib" \
  -DCOMPILER_RT_BUILD_BUILTINS=ON \
  -DCOMPILER_RT_BUILD_SANITIZERS=OFF \
  -DCOMPILER_RT_BUILD_XRAY=OFF \
  -DCOMPILER_RT_BUILD_LIBFUZZER=OFF \
  -DCOMPILER_RT_BUILD_PROFILE=OFF \
  -DCOMPILER_RT_BAREMETAL_BUILD=ON \
  -DCOMPILER_RT_OS_DIR="baremetal" \
  -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
  -DCMAKE_INSTALL_PREFIX="$1" \
  "-DCOMPILER_RT_INSTALL_PATH=$1/lib/clang/20"
ninja -j16 install
mkdir -p "$1/lib/clang/20/lib/risc32--"
cp "$1/lib/clang/20/lib/baremetal/libclang_rt.builtins-riscv32.a" \
  "$1/lib/clang/20/lib/risc32--/libclang_rt.builtins-riscv32.a"

cd "$FUCK/.."
rm -r build || true
cmake -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release \
  -S. -Bbuild -DDMM_ISA=riscv \
  "-DCMAKE_C_COMPILER=$1/bin/clang" "-DCMAKE_INSTALL_PREFIX=$1" 
ninja -C build -j16 install
bash rvRunTests.sh $1