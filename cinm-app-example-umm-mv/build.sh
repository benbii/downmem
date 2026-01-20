#!/bin/bash
# 功能：CINM到DownMem适配构建脚本
# 将CINM编译输出的DPU代码在DownMem模拟器中upmem指令集模式运行，完成从CINM框架到DownMem框架的转换

# 定义基础路径变量
BASE_PATH="/home/fengjingge/src/downmem/new2-downmem/downmem"
BASE_CINM_PATH="/home/fengjingge/src/downmem/new2-downmem/cinm/Cinnamon"

set -xe
cd "$(dirname "$0")"

rm -rf build/

# 从CINM输出目录复制生成的DPU源文件
MV_DPU_FILE="$BASE_CINM_PATH/testbench/gen/mv/irs/mv.dpu.c"
if [ -f "$MV_DPU_FILE" ]; then
    cp "$MV_DPU_FILE" ./devApp/
else
    echo "错误: 文件 $MV_DPU_FILE 不存在，跳过复制操作"
fi

DPU_SRC="devApp/mv.dpu.c"

# 修正点 1: 修改内存步长 (Stride)
# 原理: 原代码步长为 2048 (仅够放 Vector A)，导致多 Tasklet 运行时，T1 的 A 覆盖 T0 的 B。
# 修改: 将步长改为 4104 (Vector A 2048 + Vector B 2048 + Result 8)，实现数据隔离。
sed -i '/void mv_dimm8_nopt()/,/}/ s/int32_t v3 = v2 \* 2048;/int32_t v3 = v2 * 4104;/' "$DPU_SRC"

# 修正点 2: 修改结果写入地址
# 原理: 原代码 v11 计算逻辑导致所有 Tasklet 的结果都写在固定偏移 4096 处，产生竞态冲突。
# 修改: 让结果地址紧跟在当前 Tasklet 的 Vector B 之后 (相对偏移 2048)。
sed -i '/void mv_dimm8_nopt()/,/}/ s/int32_t v11 = v9 + v10;/int32_t v11 = v7 + 2048;/' "$DPU_SRC"

# 设置UPMEM环境（优先使用参数4）
U="${BASE_PATH}/third-party/upmem/upmem_env.sh"
if ! [ -z "$4" ]; then U="$4"; fi

# 设置Dmm库路径（优先使用参数3）
D="${BASE_PATH}/build/CMakeFiles/Export/lib/cmake/Dmm"
if ! [ -z "$3" ]; then D="$3"; fi

# 设置任务集数量（优先使用参数2）
T=16
if [ ! -z "$2" ] && [ "$2" -ge 1 ]; then T="$2"; fi

# 设置应用程序名称（优先使用参数1）
S=mv
if ! [ -z "$1" ]; then S="$1"; fi

mkdir -p devApp/objdumps devApp/bins

# 配置CMake项目，使用DownMem的Clang编译器
cmake -GNinja -S. -Bbuild "-DCMAKE_C_COMPILER=${BASE_PATH}/../installed/bin/clang" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DDmm_DIR=$D"
cmake --build build --config Debug

# 编译DPU端代码用于DownMem模拟器
# 使用mv_dimm8_nopt版本（对应16个tasklets）
source "$U"
#dpu-upmem-dpurte-clang -O2 devApp/$S.dpu.c -DNR_TASKLETS=16 -DCOMPILE_mv_dimm8_nopt -o devApp/bins/$S.ummbin
dpu-upmem-dpurte-clang -O2 devApp/$S.dpu.c -DNR_TASKLETS=1 -DCOMPILE_mv_dimm8_nopt -o devApp/bins/$S.ummbin

# 生成反汇编文件用于调试
llvm-objdump -t -d devApp/bins/$S.ummbin >devApp/objdumps/$S.objdump 
llvm-objdump -s -j .atomic -j .data -j .data.__sys_host -j .data.stacks -j .mram \
  devApp/bins/$S.ummbin >>devApp/objdumps/$S.objdump 

# 运行DownMem模拟器执行转换后的代码
source setup_env.sh
./build/mv 1 512 devApp/objdumps/mv.objdump

