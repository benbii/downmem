#!/usr/bin/env bash
# Source this file from the repository root before building/running UPMEM mode.

_DOWNMEM_ENV_SCRIPT="${BASH_SOURCE[0]}"
if [ -z "$_DOWNMEM_ENV_SCRIPT" ]; then
  _DOWNMEM_ENV_SCRIPT="$0"
fi

export DOWNMEM_ROOT="$(cd "$(dirname "$_DOWNMEM_ENV_SCRIPT")" && pwd)"
export DOWNMEM_LOCALDEPS="$DOWNMEM_ROOT/third-party/localdeps/usr"
export UPMEM_HOME="$DOWNMEM_ROOT/third-party/upmem"
export Dmm_DIR="$DOWNMEM_ROOT/installed-upmem/lib/cmake/Dmm"

export PATH="$HOME/.local/bin:$DOWNMEM_LOCALDEPS/bin:$UPMEM_HOME/bin:$PATH"
export PKG_CONFIG_PATH="$DOWNMEM_LOCALDEPS/lib/x86_64-linux-gnu/pkgconfig:$DOWNMEM_LOCALDEPS/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$UPMEM_HOME/lib:$DOWNMEM_LOCALDEPS/lib/x86_64-linux-gnu:$DOWNMEM_LOCALDEPS/lib/llvm-12/lib:$DOWNMEM_ROOT/installed-upmem/lib:${LD_LIBRARY_PATH:-}"

export DOWNMEM_CMAKE_C_COMPILER="$UPMEM_HOME/bin/clang"
export DOWNMEM_CMAKE_CXX_COMPILER="$UPMEM_HOME/bin/clang++"
export DOWNMEM_OPENMP_INCLUDE="$DOWNMEM_LOCALDEPS/lib/llvm-12/lib/clang/12.0.1/include"
export DOWNMEM_OPENMP_LIBRARY="$DOWNMEM_LOCALDEPS/lib/llvm-12/lib/libomp.so"

if [ ! -x "$UPMEM_HOME/bin/clang" ]; then
  echo "warning: UPMEM SDK not found at $UPMEM_HOME" >&2
fi
if [ ! -f "$DOWNMEM_OPENMP_LIBRARY" ]; then
  echo "warning: local OpenMP runtime not found at $DOWNMEM_OPENMP_LIBRARY" >&2
fi
