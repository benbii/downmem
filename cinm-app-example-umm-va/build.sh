#!/bin/bash
# 功能：CINM到DownMem适配构建脚本
# 将CINM编译输出的DPU代码在DownMem模拟器中运行，完成从CINM框架到DownMem框架的转换

# 定义基础路径变量
BASE_PATH="/home/fengjingge/src/downmem/new2-downmem/downmem"
BASE_CINM_PATH="/home/fengjingge/src/downmem/new2-downmem/cinm/Cinnamon"

set -xe
cd "$(dirname "$0")"

rm -rf build/

# 从CINM输出目录复制生成的DPU源文件
VA_DPU_FILE="$BASE_CINM_PATH/testbench/gen/va/irs/va.dpu.c"
if [ -f "$VA_DPU_FILE" ]; then
    cp "$VA_DPU_FILE" ./devApp/
else
    echo "错误: 文件 $VA_DPU_FILE 不存在，跳过复制操作"
fi

# 设置UPMEM环境（优先使用参数4）
U="${BASE_PATH}/third-party/upmem/upmem_env.sh"
if ! [ -z "$4" ]; then U="$4"; fi

# 设置Dmm库路径（优先使用参数3）
D="${BASE_PATH}/build/CMakeFiles/Export/lib/cmake/Dmm"
if ! [ -z "$3" ]; then D="$3"; fi

# 设置任务集数量（优先使用参数2）
T=16
if [ "$2" -ge 1 ]; then T="$2"; fi

# 设置应用程序名称（优先使用参数1）
S=va
if ! [ -z "$1" ]; then S="$1"; fi

mkdir -p devApp/objdumps devApp/bins

# 配置CMake项目，使用DownMem的Clang编译器
cmake -GNinja -S. -Bbuild "-DCMAKE_C_COMPILER=${BASE_PATH}/../installed/bin/clang" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DDmm_DIR=$D"
cmake --build build --config Debug

# 编译DPU端代码用于DownMem模拟器
source "$U"
dpu-upmem-dpurte-clang -O2 devApp/$S.dpu.c -DNR_TASKLETS=16 -DCOMPILE_va_8 -o devApp/bins/$S.ummbin

# 生成反汇编文件用于调试
llvm-objdump -t -d devApp/bins/$S.ummbin >devApp/objdumps/$S.objdump 
llvm-objdump -s -j .atomic -j .data -j .data.__sys_host -j .data.stacks -j .mram \
  devApp/bins/$S.ummbin >>devApp/objdumps/$S.objdump 

# 运行DownMem模拟器执行转换后的代码
source setup_env.sh
./build/va 1 256 devApp/objdumps/va.objdump