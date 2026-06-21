# llama.cpp Downmem Offload Design

Date: 2026-06-18

## Goal

Run a real small model through `llama.cpp` while offloading a restricted subset
of expensive `GGML_OP_MUL_MAT` work to the `downmem` RV processing-in-memory
simulator. The host still performs model loading, tokenization, graph
scheduling, unsupported operators, KV cache handling, sampling, and CPU
fallback. The downmem path returns computed results to the ggml graph so the
full model can continue executing normally.

The MVP targets `F32 x F32 -> F32` `MUL_MAT` in a GEMV-style shape:
`src0 [K, N] * src1 [K, 1] -> dst [N, 1]`. Quantized weight support is limited
to an optional host-side dequantization experiment behind an environment
variable.

## Non-Goals

- Do not offload the full model or full transformer layer.
- Do not store the model permanently inside downmem MRAM.
- Do not implement attention, softmax, KV cache, sampling, or activation
  functions on DPU.
- Do not implement general batched GEMM in the first version.
- Do not implement DPU-side quantized dot products in the first version.
- Do not add public CLI flags; use environment variables only.

## Source Facts and Constraints

The design is based on the local source trees:

- `llama.cpp`: `/home/fjg/src/llama.cpp`
- `downmem`: `/home/fjg/src/downmem`

Relevant downmem facts:

- RV DPUs have 64 KB WRAM and 64 MB MRAM.
- `dpu_load` loads RV ELF sections and symbols from a DPU binary.
- `dpu_push_xfer` and `dpu_broadcast_to` copy host data directly into each DPU's
  simulated `WMAram`.
- For RV MRAM symbols, host copy addresses are translated to
  `WramSizeR + (symbol_addr - MramBeginR) + offset`.
- A single `dpu_push_xfer` uses one uniform transfer length for every DPU in the
  set. Per-DPU row blocks must therefore be padded to a common size.
- DPU code uses `mram_read` and `mram_write`, implemented as custom
  `ldmram`/`sdmram` instructions, to move data between MRAM and WRAM.
- The RV simulator implements RV32 integer, multiply/divide, bitmanip, CSR, and
  custom MRAM DMA instructions. It does not implement hardware floating-point
  instructions.
- Float code can still run through the RV soft-float ABI and compiler runtime,
  as demonstrated by `OPDEMOF`, but performance and code size must be treated
  conservatively.

Relevant llama.cpp facts:

- ggml supports backend registration with device `supports_op`, `offload_op`,
  and `graph_compute` callbacks.
- `llama_context` initializes registered `ACCEL` backends before the CPU
  backend.
- The backend scheduler can offload host-buffer weight operations to a higher
  priority backend when `op_offload` is enabled and that backend's `offload_op`
  returns true.
- A backend that uses `ggml_backend_cpu_buffer_type()` can share host buffers
  with CPU-like accelerator backends, following the BLAS backend pattern.

## Architecture

Add a `ggml-downmem` backend to `llama.cpp`, modeled on the existing BLAS
backend shape.

The backend registers as `GGML_BACKEND_DEVICE_TYPE_ACCEL` and uses the CPU host
buffer type. It is loaded by normal backend initialization, but it only opts in
to computation when `GGML_DOWNMEM=1`.

The backend is responsible for:

- Selecting only supported `GGML_OP_MUL_MAT` nodes.
- Preparing contiguous float staging buffers for inputs.
- Allocating and loading a downmem RV DPU set.
- Transferring per-DPU matrix row blocks, broadcasting the vector, launching
  the DPU program, collecting output rows, and writing the result tensor.
- Emitting optional logs and validation summaries.

The downmem repository adds a dedicated RV DPU kernel named `LLAMA_GEMV_F32`,
plus a host-side standalone test. The DPU kernel accepts only plain arguments
and MRAM buffers; it has no dependency on ggml tensor metadata.

## Backend Components

`ggml-downmem` contains:

- Backend registration and device descriptors.
- `supports_op`: returns true only for enabled, supported `MUL_MAT` shapes.
- `offload_op`: returns true only when the operation should be claimed from CPU
  host buffers.
- `graph_compute`: executes no-op/view nodes locally and dispatches target
  `MUL_MAT` nodes to the downmem bridge.
- Downmem bridge context: DPU set, loaded binary path, DPU count, staging
  buffers, counters, and validation configuration.
- Tensor staging helpers: contiguous float extraction and optional host-side
  dequantization through ggml type traits.
- Logging and validation helpers.

The downmem RV kernel contains:

- A `DPU_INPUT_ARGUMENTS` struct in WRAM for `k`, `k_pad`, `n_rows`,
  `max_rows`, and fixed MRAM offsets.
- MRAM layout logic for `A`, `x`, and `y` segments.
- Tasklet work partitioning over local output rows.
- Chunked `mram_read` of `A` and `x` into small WRAM buffers.
- Soft-float accumulation and `mram_write` of output rows.

## Data Flow

The first version supports only GEMV-style `MUL_MAT`:

```text
src0: [K, N]  weight matrix in ggml layout
src1: [K, 1]  activation vector
dst:  [N, 1]  output vector
```

The host bridge stages data as:

```text
A_rows: [N_pad, K_pad] row-major floats
x:      [K_pad]        contiguous floats
y:      [N_pad]        contiguous floats per DPU row block
```

Steps:

1. Reject unsupported shapes unless a debugging mode explicitly enables column
   looping.
2. Convert or copy `src0` into row-major float `A_rows`.
3. Copy `src1` into float `x`.
4. Split `A_rows` by output rows across `nr_dpus`.
5. Pad each DPU block to `max_rows_per_dpu * k_pad`; choose `k_pad` so MRAM
   chunk reads and host transfers remain aligned and in bounds.
6. Transfer per-DPU `A` blocks to `DPU_MRAM_HEAP_POINTER_NAME` offset 0.
7. Broadcast `x` to offset `max_rows_per_dpu * k_pad * sizeof(float)`.
8. Transfer per-DPU `DPU_INPUT_ARGUMENTS`.
9. Launch DPUs synchronously.
10. Pull `y` from offset after `A` and `x`.
11. Strip padding and write rows back to ggml `dst`.

DPU MRAM layout per DPU:

```text
offset 0:
  A block, max_rows_per_dpu * k_pad * sizeof(float)

offset a_bytes:
  x vector, k_pad * sizeof(float)

offset a_bytes + x_bytes:
  y block, max_rows_per_dpu * sizeof(float)
```

The DPU kernel never sees ggml tensor objects. It only operates on this fixed
layout and its argument struct.

## Supported and Fallback Cases

Supported by default:

- `GGML_OP_MUL_MAT`
- `dst` type `GGML_TYPE_F32`
- `src1` type `GGML_TYPE_F32`
- `src1->ne[1] == 1`
- `src0` is `GGML_TYPE_F32`, unless host dequantization is explicitly enabled
- tensor views are either contiguous or rejected
- matrix/vector sizes fit configured staging limits and DPU MRAM limits

Fallback to CPU:

- `GGML_DOWNMEM` unset or disabled
- non-`MUL_MAT` ops
- batched `M > 1` unless debug column-loop mode is explicitly enabled
- unsupported layouts or tensor types
- missing DPU binary
- dimension or memory limit violations
- any unsupported quantization when host dequantization is disabled

Fallback should happen at `supports_op`/`offload_op` time whenever possible. If
an op has already been scheduled to downmem and execution fails, return
`GGML_STATUS_FAILED` rather than silently recomputing on CPU inside
`graph_compute`.

## Configuration

No CLI flags are added. Configuration is through environment variables:

- `GGML_DOWNMEM=1`: enable downmem backend selection.
- `GGML_DOWNMEM_DPU_BIN=/path/to/rvbin`: path to the RV DPU kernel.
- `GGML_DOWNMEM_NR_DPUS=4`: number of simulated DPUs.
- `GGML_DOWNMEM_MAX_OPS=N`: maximum offloaded ops per process or context.
- `GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1`: allow host-side dequantization of
  `src0` into float staging.
- `GGML_DOWNMEM_VALIDATE=1`: compare offloaded output against a host float
  reference and fail if outside tolerance.
- `GGML_DOWNMEM_VERBOSE=1`: print per-op shape, type, transfer size, DPU count,
  timing records, and validation summary.

The default behavior with no environment variables must match the existing
llama.cpp CPU behavior.

## Error Handling

- Backend registration should not fail the process if the downmem binary is
  absent. It should register but decline operations unless enabled and valid.
- DPU setup failures before scheduling should cause `supports_op`/`offload_op`
  to decline.
- Runtime transfer or launch failures after scheduling should return
  `GGML_STATUS_FAILED`.
- Validation failures with `GGML_DOWNMEM_VALIDATE=1` should return
  `GGML_STATUS_FAILED`.
- Logs should include enough shape and environment detail to reproduce failures.
- Existing user changes in each git worktree must not be overwritten.

## Validation Plan

### 1. Downmem Kernel Test

Add a standalone host test in `downmem` that:

- Builds and loads the RV `LLAMA_GEMV_F32` kernel.
- Generates deterministic float matrices and vectors.
- Tests single-DPU and multi-DPU cases.
- Tests `N` not divisible by DPU count.
- Tests `K` requiring padding.
- Compares with host float GEMV using a tolerance such as `abs <= 1e-4` plus a
  relative threshold for larger values.

### 2. ggml Backend Test

Add a small ggml-level test or script that:

- Constructs one `GGML_OP_MUL_MAT` graph with `F32 x F32 -> F32`.
- Runs CPU baseline.
- Runs with `GGML_DOWNMEM=1`.
- Compares output tensors.
- Repeats with `GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1` if quantized input testing
  is enabled.

### 3. llama.cpp End-to-End Test

Use the local model:

```text
/home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf
```

Run a CPU baseline and a downmem-enabled pass with fixed prompt, greedy sampler,
fixed thread count, and a low `GGML_DOWNMEM_MAX_OPS` value.

First-stage success:

- Inference completes.
- Logs show at least one `MUL_MAT` offload when enabled.
- CPU-disabled default behavior remains unchanged.

Second-stage success:

- Pick a stable prompt and settings.
- Verify generated token sequence matches CPU baseline.
- Keep numeric validation available for diagnosing token mismatch.

### 4. Negative Tests

- No `GGML_DOWNMEM`: no downmem logs and no behavior change.
- Invalid `GGML_DOWNMEM_DPU_BIN`: no crash; supported ops are declined before
  scheduling.
- `GGML_DOWNMEM_VALIDATE=1` with intentionally corrupted output: validation
  fails.
- Unsupported `M > 1` without debug mode: CPU fallback.

## Version Control Plan

Two repositories will be changed in implementation:

- `/home/fjg/src/downmem`: DPU kernel, host test, build entries, scripts.
- `/home/fjg/src/llama.cpp`: `ggml-downmem` backend, CMake integration, test or
  validation scripts.

Before implementation, record `git status` in both repositories. In `downmem`,
the existing `.gitignore` modification and untracked
`setup_downmem_upmem_env.sh` must be preserved and not reverted.

The design document itself is committed in the `downmem` repository before
implementation planning.
