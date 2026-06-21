# LLAMA_GEMV_F32 Downmem Offload 技术文档

本文整理 `0002` 到 `0008` 这组 patch 的目标、原理、代码修改、当前效果和使用方式。它们的主线是：为 `llama.cpp` 的矩阵向量乘法 offload 做准备，在 `downmem` 中先实现并验证一个独立的 RISC-V DPU `F32` GEMV kernel。

## 1. 它是干什么的

这组 patch 面向一个更大的目标：让 `llama.cpp` 在正常 CPU 推理流程中，把一部分 `GGML_OP_MUL_MAT` 工作 offload 到 `downmem` RV processing-in-memory 模拟器执行，再把结果写回 ggml graph。

当前这组 patch 在 `downmem` 仓库已经实现的部分是：

- 新增一个 DPU 端 kernel：`devApp/LLAMA_GEMV_F32.c`
- 新增一个 host 端独立正确性测试：`hostApp/LLAMA_GEMV_F32.c`
- 新增 CMake 构建入口：`rvLLAMA_GEMV_F32` 和 `dmmLLAMA_GEMV_F32`
- 新增一键 smoke 脚本：`scripts/run-llama-gemv-f32-test.sh`
- 将 `dpu_free()` 的运行统计从 stdout 改到 stderr，避免污染测试程序的标准输出
- 新增设计文档和实现计划，描述后续 `llama.cpp` `ggml-downmem` backend 如何接入

也就是说，当前 patch 已经把 DPU GEMV kernel 和独立验证链路跑通；`llama.cpp` 端的 `ggml-downmem` backend 仍属于设计/计划内容，不在这组 downmem patch 的实际代码变更范围内。

## 2. 支持的计算形式

MVP 目标只支持 `F32 x F32 -> F32` 的 GEMV 形态：

```text
src0: [K, N]  权重矩阵
src1: [K, 1]  输入向量
dst:  [N, 1]  输出向量
```

host 侧会把它整理成 DPU kernel 更容易处理的 row-major 布局：

```text
A_rows: [N_pad, K_pad]
x:      [K_pad]
y:      [N_pad]
```

其中：

- `N` 是输出行数，也就是最终输出向量长度。
- `K` 是每个 dot product 的长度。
- `N` 会按行切分给多个 DPU。
- `K` 在 host 测试中按 2 对齐成 `k_pad`，padding 部分不参与计算。
- 每个 DPU 的矩阵块按同一个 `max_rows` padding，因为 `dpu_push_xfer()` 对一个 DPU set 发起传输时，每个 DPU 使用相同传输长度。

## 3. 核心原理

### 3.1 Host 到 DPU 的数据布局

每个 DPU 的 MRAM 使用固定布局：

```text
offset 0:
  A block, max_rows * k_pad * sizeof(float)

offset a_bytes:
  x vector, k_pad * sizeof(float)

offset a_bytes + x_bytes:
  y block, max_rows * sizeof(float)
```

DPU 不理解 ggml tensor，也不依赖 llama.cpp 的元数据。host 只通过一个 WRAM 参数结构告诉 DPU 当前这次 GEMV 的形状和 MRAM 偏移：

```c
typedef struct {
  uint32_t k;
  uint32_t k_pad;
  uint32_t n_rows;
  uint32_t max_rows;
  uint32_t a_offset;
  uint32_t x_offset;
  uint32_t y_offset;
} llama_gemv_f32_args_t;
```

这个结构通过符号名 `DPU_INPUT_ARGUMENTS` 写入 DPU WRAM。

### 3.2 多 DPU 行切分

host 测试按输出行 `N` 分配工作：

```text
base = N / nr_dpus
rest = N % nr_dpus
rows[d] = base + (d < rest ? 1 : 0)
```

因此可以覆盖 `N` 不能被 DPU 数整除的情况。若 DPU 数大于输出行数，部分 DPU 的 `n_rows` 为 0，kernel 会自然跳过。

### 3.3 DPU 内部 tasklet 并行

DPU kernel 使用 16 个 tasklet 构建。每个 DPU 收到自己的局部行块后，再把 `n_rows` 分给 tasklet：

```text
tasklet 0 -> 本 DPU 的一部分 rows
tasklet 1 -> 本 DPU 的一部分 rows
...
```

每个 tasklet 对自己负责的每一行做一次 dot product：

```text
y[row] = sum(A[row, col] * x[col]), col = 0..K-1
```

`K` 维按 64 个 float 一块读取：

- 从 MRAM 读取一段 `A`
- 从 MRAM 读取对应一段 `x`
- 在 WRAM buffer 中做 soft-float 累加
- 最后把一个 `float` 结果写回 MRAM 的 `y` 区

由于当前 RV DPU 模拟器没有硬件浮点指令，`float` 计算依赖 soft-float ABI 和编译器运行时，正确性优先，性能需要保守看待。

## 4. Patch 逐项说明

| Patch | 作用 | 主要文件 |
| --- | --- | --- |
| `0002` | 新增 llama.cpp downmem offload 总体设计 | `docs/superpowers/specs/2026-06-18-llama-downmem-offload-design.md` |
| `0003` | 新增详细实现计划，覆盖 downmem kernel、host test 和未来 llama.cpp backend | `docs/superpowers/plans/2026-06-18-llama-downmem-offload.md` |
| `0004` | 新增 `LLAMA_GEMV_F32` DPU kernel scaffold，并接入 RV DPU 构建 | `devApp/LLAMA_GEMV_F32.c`, `devApp/CMakeLists.txt` |
| `0005` | 新增先失败的 host 正确性测试，并接入 host 构建 | `hostApp/LLAMA_GEMV_F32.c`, `CMakeLists.txt` |
| `0006` | 实现 DPU 端 F32 GEMV 计算逻辑 | `devApp/LLAMA_GEMV_F32.c` |
| `0007` | 新增一键构建和运行 smoke 脚本 | `scripts/run-llama-gemv-f32-test.sh` |
| `0008` | 将 downmem runtime stats 输出从 stdout 改为 stderr | `ummHostApi.c` |

## 5. 修改了什么

### 5.1 `devApp/LLAMA_GEMV_F32.c`

新增 DPU 端 GEMV kernel：

- 定义 host 参数结构 `llama_gemv_f32_args_t`
- 暴露 `__host llama_gemv_f32_args_t DPU_INPUT_ARGUMENTS`
- 使用 `ALL_THREADS_BARRIER_INIT()` 做启动同步
- 使用 `me()` 获取 tasklet ID
- 使用 `mram_read()` 从 MRAM 读入 `A` 和 `x`
- 使用 `mram_write()` 把 `y` 写回 MRAM
- 每次最多读取 `LLAMA_GEMV_CHUNK_FLOATS = 64` 个 float，降低 WRAM 占用

### 5.2 `hostApp/LLAMA_GEMV_F32.c`

新增 host 端测试程序 `dmmLLAMA_GEMV_F32`，负责：

- 分配指定数量的模拟 DPU
- 加载 RV DPU binary
- 构造 deterministic matrix/vector 输入
- 计算 host 参考 GEMV
- 按 DPU 行切分并 padding `A`
- 写入 `DPU_INPUT_ARGUMENTS`
- 传输 `A`、广播 `x`
- 启动 DPU
- 拉回 `y`
- 与 host 参考结果比较

测试覆盖 4 类情况：

```text
n=8,  k=16, dpus=1   单 DPU
n=17, k=31, dpus=4   行数不能整除 DPU 数，K 需要 padding
n=33, k=64, dpus=6   多 DPU，较大 K
n=3,  k=7,  dpus=8   DPU 数大于输出行数
```

比较阈值：

```text
abs(diff) <= 1e-4
或
abs(diff) <= max(1, abs(expected)) * 1e-4
```

### 5.3 `CMakeLists.txt`

新增 host 可执行文件：

```cmake
add_executable(dmmLLAMA_GEMV_F32 hostApp/LLAMA_GEMV_F32.c)
target_link_libraries(dmmLLAMA_GEMV_F32 PRIVATE dmm m)
```

### 5.4 `devApp/CMakeLists.txt`

新增 RV DPU binary target：

```cmake
add_executable(rvLLAMA_GEMV_F32 LLAMA_GEMV_F32.c)
rvbin_make(rvLLAMA_GEMV_F32 16 -flto -O3)
add_dependencies(dpuExamples rvLLAMA_GEMV_F32)
```

构建产物路径为：

```text
build-rv/devApp/rvbins/LLAMA_GEMV_F32
```

### 5.5 `scripts/run-llama-gemv-f32-test.sh`

新增一键 smoke 脚本：

```bash
./scripts/run-llama-gemv-f32-test.sh
```

脚本会：

1. 构建 `rvLLAMA_GEMV_F32`
2. 构建 `dmmLLAMA_GEMV_F32`
3. 检查 DPU binary 是否存在
4. 设置默认 `DMM_NR_SIM_THRDS=4`
5. 运行 host 测试

### 5.6 `ummHostApi.c`

将 DPU 释放时打印的累计统计从 stdout 改为 stderr：

```text
Freed <N> DPU, Exec <...>usec, Xfer <...>usec till now
```

这样测试程序的 stdout 可以只保留类似 `LLAMA_GEMV_F32 passed` 的业务结果，便于脚本解析。

## 6. 怎么使用

### 6.1 前置条件

需要已经完成 downmem RV 构建环境配置，并存在 `build-rv` 构建目录。当前 smoke 脚本默认使用：

```text
build-rv/
```

如果还没有构建 RV 工具链和 downmem，可先参考仓库根目录的 `README.md` 和 `install.md`。

### 6.2 一键运行

在仓库根目录执行：

```bash
./scripts/run-llama-gemv-f32-test.sh
```

期望输出包含：

```text
LLAMA_GEMV_F32 passed
```

运行统计会输出到 stderr，例如：

```text
Freed 4 DPU, Exec ...usec, Xfer ...usec till now
```

### 6.3 手动构建和运行

也可以手动执行：

```bash
cmake --build build-rv --target rvLLAMA_GEMV_F32 dmmLLAMA_GEMV_F32 -j"$(nproc)"

DMM_NR_SIM_THRDS=4 \
  ./build-rv/dmmLLAMA_GEMV_F32 \
  ./build-rv/devApp/rvbins/LLAMA_GEMV_F32
```

如果只想捕获测试 stdout，并把运行统计单独保存：

```bash
DMM_NR_SIM_THRDS=4 \
  ./build-rv/dmmLLAMA_GEMV_F32 \
  ./build-rv/devApp/rvbins/LLAMA_GEMV_F32 \
  > llama-gemv.out \
  2> llama-gemv.stats
```

## 7. 当前实现效果

当前工作区已运行：

```bash
./scripts/run-llama-gemv-f32-test.sh
```

结果：

```text
ninja: no work to do.
Freed 1 DPU, Exec 91usec, Xfer 120usec till now
Freed 4 DPU, Exec 240usec, Xfer 240usec till now
Freed 6 DPU, Exec 547usec, Xfer 360usec till now
Freed 8 DPU, Exec 582usec, Xfer 496usec till now
LLAMA_GEMV_F32 passed
```

说明：

- DPU binary 能正常加载和执行。
- 单 DPU、多 DPU、非整除行分配、`K` padding、DPU 数大于行数等 case 均通过。
- DPU 结果与 host F32 GEMV 参考结果在 `1e-4` 误差范围内一致。
- runtime stats 已经从 stderr 打印，不再污染 stdout 的 pass/fail 文本。

## 8. 与 llama.cpp offload 的关系

`0002` 和 `0003` 描述的最终接入方式是新增一个 `ggml-downmem` backend：

- 注册为 `GGML_BACKEND_DEVICE_TYPE_ACCEL`
- 默认不启用，只有 `GGML_DOWNMEM=1` 时才尝试 offload
- 只 claim 支持的 `GGML_OP_MUL_MAT`
- 把 ggml tensor staging 成本文档中的 `A/x/y` MRAM 布局
- 调用 downmem API 执行 DPU kernel
- 把结果写回 ggml `dst`

计划中的关键环境变量包括：

```bash
export GGML_DOWNMEM=1
export GGML_DOWNMEM_DPU_BIN=/home/fjg/src/downmem/build-rv/devApp/rvbins/LLAMA_GEMV_F32
export GGML_DOWNMEM_NR_DPUS=4
export GGML_DOWNMEM_MAX_OPS=1
export GGML_DOWNMEM_VERBOSE=1
```

如果使用量化模型，设计中还规划了 host 侧反量化开关：

```bash
export GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1
```

但需要注意：这些 `GGML_DOWNMEM_*` 变量属于后续 llama.cpp backend 设计。当前这组 downmem patch 本身不读取这些变量，也没有修改 llama.cpp 源码。

## 9. 当前限制

- 只实现 `F32` GEMV kernel，不支持 DPU 端量化 dot product。
- 不支持通用 batched GEMM。
- 不把模型权重常驻 MRAM；每次测试都会传输当前矩阵块。
- DPU kernel 不处理 ggml tensor metadata，只处理 host 已经整理好的扁平 MRAM 布局。
- RV DPU 浮点通过 soft-float 执行，性能不能等同硬件浮点。
- standalone host 测试主要用于正确性验证，不是完整生产级错误处理。
- 当前 patch 只完成 downmem 侧 kernel/test/smoke，不包含 llama.cpp backend 实现。

## 10. 后续建议

后续如果继续推进 llama.cpp offload，建议按当前计划顺序做：

1. 在 llama.cpp 中新增 `ggml-downmem` backend scaffold。
2. 先只支持 `F32 x F32 -> F32` GEMV，并添加 ggml-level correctness test。
3. 接入本文档中的 `LLAMA_GEMV_F32` DPU binary。
4. 加 `GGML_DOWNMEM_VALIDATE=1` 路径，对比 host reference。
5. 再考虑 host 侧量化权重反量化，服务 `stories15M-q4_0.gguf` 这类量化模型 smoke。
6. 最后再评估性能瓶颈：host staging、MRAM transfer、soft-float、DPU 数量和 chunk 大小。
