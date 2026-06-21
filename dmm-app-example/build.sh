#!/bin/bash

# 定义基础路径变量
BASE_PATH="/home/fengjingge/src/downmem/new2-downmem/downmem"

set -xe
cd "$(dirname "$0")"
#U="${HOME}/.local/upmem-2024.2.0-Linux-x86_64/upmem_env.sh"
U="${BASE_PATH}/third-party/upmem/upmem_env.sh"
if ! [ -z "$4" ]; then U="$4"; fi
#D="${HOME}/.local/dmm/lib/cmake/Dmm"
D="${BASE_PATH}/build/CMakeFiles/Export/lib/cmake/Dmm"
if ! [ -z "$3" ]; then D="$3"; fi
T=16
if [ "$2" -ge 1 ]; then T="$2"; fi
S=mydev2
if ! [ -z "$1" ]; then S="$1"; fi

mkdir -p devApp/objdumps devApp/bins

#设置CMAKE_C_COMPILER为编译器路径
#cmake -GNinja -S. -Bbuild -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DDmm_DIR=$D"
cmake -GNinja -S. -Bbuild "-DCMAKE_C_COMPILER=${BASE_PATH}/../installed/bin/clang" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DDmm_DIR=$D"
cmake --build build --config Debug
source "$U"
dpu-upmem-dpurte-clang -O2 devApp/$S.c -DNR_TASKLETS=16 -o devApp/bins/$S.ummbin
llvm-objdump -t -d devApp/bins/$S.ummbin >devApp/objdumps/$S.objdump 
llvm-objdump -s -j .atomic -j .data -j .data.__sys_host -j .data.stacks -j .mram \
  devApp/bins/$S.ummbin >>devApp/objdumps/$S.objdump 