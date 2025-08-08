#!/bin/bash
set -xe
cd "$(dirname "$0")"
U="${HOME}/.local/upmem-2024.2.0-Linux-x86_64/upmem_env.sh"
if ! [ -z "$4" ]; then U="$4"; fi
D="${HOME}/.local/dmm/lib/cmake/Dmm"
if ! [ -z "$3" ]; then D="$3"; fi
T=16
if [ "$2" -ge 1 ]; then T="$2"; fi
S=mydev2
if ! [ -z "$1" ]; then S="$1"; fi

mkdir -p devApp/objdumps devApp/bins
cmake -GNinja -S. -Bbuild -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DDmm_DIR=$D"
cmake --build build --config Debug
source "$U"
dpu-upmem-dpurte-clang -O2 devApp/$S.c -DNR_TASKLETS=16 -o devApp/bins/$S.ummbin
llvm-objdump -t -d devApp/bins/$S.ummbin >devApp/objdumps/$S.objdump 
llvm-objdump -s -j .atomic -j .data -j .data.__sys_host -j .data.stacks -j .mram \
  devApp/bins/$S.ummbin >>devApp/objdumps/$S.objdump 