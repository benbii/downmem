#!/bin/bash

# 功能：CINM到DownMem适配构建脚本
# 将CINM编译输出的DPU代码在DownMem模拟器中rv指令集模式运行，完成从CINM框架到DownMem框架的转换

set -xe
cd "$(dirname "$0")"

# 定义基础路径变量
BASE_PATH="/home/fengjingge/src/downmem/new2-downmem/downmem"
BASE_CINM_PATH="/home/fengjingge/src/downmem/new2-downmem/cinm/Cinnamon"

rm -rf build/

# 从CINM输出目录复制生成的DPU源文件
VA_DPU_FILE="$BASE_CINM_PATH/testbench/gen/va/irs/va.dpu.c"
if [ -f "$VA_DPU_FILE" ]; then
    cp "$VA_DPU_FILE" ./devApp/
else
    echo "错误: 文件 $VA_DPU_FILE 不存在，跳过复制操作"
fi

# 修改 ./devApp/va.dpu.c 文件
# 默认va.dpu.c包含了rv不支持的头文件，如果expf.c、perfcounter.h等需要删除
# 默认va.dpu.c 采用int32_t v5[256]申请栈空间，但是由于1个tasklet只有1024B，多个数组会越界，因此修改为mem_alloc堆申请

MODIFIED_FILE="./devApp/va.dpu.c"
if [ -f "$MODIFIED_FILE" ]; then
    # 使用sed删除指定的include行
    sed -i '/#include <perfcounter.h>/d; /#include "expf.c"/d; /#include <stdio.h>/d' "$MODIFIED_FILE"
    
    # 替换 __dma_aligned 数组声明为 mem_alloc 动态分配
    # 对于 va_8 函数中的数组
    sed -i 's/__dma_aligned int32_t v5\[256\];/int32_t *v5 = mem_alloc(256 * sizeof(int32_t));/g' "$MODIFIED_FILE"
    sed -i 's/__dma_aligned int32_t v8\[256\];/int32_t *v8 = mem_alloc(256 * sizeof(int32_t));/g' "$MODIFIED_FILE"
    sed -i 's/__dma_aligned int32_t v11\[256\];/int32_t *v11 = mem_alloc(256 * sizeof(int32_t));/g' "$MODIFIED_FILE"
    
    # 对于 va_16 函数中的数组
    sed -i 's/__dma_aligned int32_t v5\[512\];/int32_t *v5 = mem_alloc(512 * sizeof(int32_t));/g' "$MODIFIED_FILE"
    sed -i 's/__dma_aligned int32_t v8\[512\];/int32_t *v8 = mem_alloc(512 * sizeof(int32_t));/g' "$MODIFIED_FILE"
    sed -i 's/__dma_aligned int32_t v11\[512\];/int32_t *v11 = mem_alloc(512 * sizeof(int32_t));/g' "$MODIFIED_FILE"
    
    echo "成功修改文件: $MODIFIED_FILE"
else
    echo "错误: 文件 $MODIFIED_FILE 不存在，无法修改"
    exit 1
fi

if test -n "$1"; then
  mkdir -p build
  cmake -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release \
    -S. -Bbuild "-DCMAKE_C_COMPILER=$1/bin/clang" 
  ninja -C build all dpuExamples
  # use libomp from this llvm installation
  export LD_LIBRARY_PATH="$1/lib/x86_64-pc-linux-gnu:${LD_LIBRARY_PATH}"
fi

time build/dmmVA 1 256 build/devApp/rvbins/VA 
