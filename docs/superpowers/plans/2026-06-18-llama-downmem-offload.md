# llama.cpp Downmem Offload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an MVP where `llama.cpp` runs normally on CPU but offloads a restricted `GGML_OP_MUL_MAT` `F32 x F32 -> F32` GEMV path to the local `downmem` RV simulator and returns the DPU result to the ggml graph.

**Architecture:** Add a dedicated `LLAMA_GEMV_F32` DPU kernel and deterministic host test in `/home/fjg/src/downmem`. Add a `ggml-downmem` ACCEL backend in `/home/fjg/src/llama.cpp` that uses CPU host buffers, opts in only through environment variables, stages tensors into a fixed MRAM layout, calls downmem APIs, and declines unsupported ops before scheduling.

**Tech Stack:** CMake, C/C++17, ggml backend API, `llama.cpp`, downmem host API (`dpu_alloc`, `dpu_load`, `dpu_push_xfer`, `dpu_launch`), RV DPU runtime (`mram_read`, `mram_write`, soft-float).

---

## File Map

### `/home/fjg/src/downmem`

- Create: `devApp/LLAMA_GEMV_F32.c`
  - RV DPU kernel for one GEMV operation.
  - Reads `DPU_INPUT_ARGUMENTS` from WRAM.
  - Reads `A`, `x`, and writes `y` under `DPU_MRAM_HEAP_POINTER_NAME`.
  - Uses only `float`, `uint32_t`, `mram_read`, `mram_write`, tasklet ID, and barriers.

- Create: `hostApp/LLAMA_GEMV_F32.c`
  - Standalone deterministic host test for the DPU kernel.
  - Builds row blocks with padding because `dpu_push_xfer` uses uniform length.
  - Runs multiple cases: single DPU, multi-DPU, uneven rows, and padded `K`.
  - Compares DPU result to host GEMV with absolute and relative tolerance.

- Modify: `CMakeLists.txt`
  - Add `dmmLLAMA_GEMV_F32` host executable.
  - Link it to `dmm` and `m`.

- Modify: `devApp/CMakeLists.txt`
  - Add RV target `rvLLAMA_GEMV_F32`.
  - Keep it out of UPMEM paths.

- Create: `scripts/run-llama-gemv-f32-test.sh`
  - Convenience smoke script for building and running the standalone downmem test.

- Preserve:
  - Do not revert or overwrite existing modified `.gitignore`.
  - Do not remove or edit untracked `setup_downmem_upmem_env.sh` unless the user explicitly asks.

### `/home/fjg/src/llama.cpp`

- Create: `ggml/src/ggml-downmem/ggml-downmem.h`
  - Public backend registration declaration: `GGML_BACKEND_API ggml_backend_reg_t ggml_backend_downmem_reg(void);`

- Create: `ggml/src/ggml-downmem/ggml-downmem.cpp`
  - Backend implementation.
  - Device registration as `GGML_BACKEND_DEVICE_TYPE_ACCEL`.
  - CPU host buffer type.
  - Environment parsing.
  - Shape/type gating for `MUL_MAT`.
  - Downmem bridge and staging.
  - Optional validation against host float GEMV.

- Create: `ggml/src/ggml-downmem/CMakeLists.txt`
  - Builds `ggml-downmem`.
  - Links to `/home/fjg/src/downmem/build-rv/libdmm.a` or cache-provided `GGML_DOWNMEM_LIB`.
  - Adds include path from cache-provided `GGML_DOWNMEM_ROOT`.
  - Links `m`, `elf`, OpenMP, PCRE2, and `numa` on Linux as required by downmem.

- Modify: `ggml/CMakeLists.txt`
  - Add `option(GGML_DOWNMEM "ggml: use downmem simulator backend" OFF)`.
  - Add cache variables:
    - `GGML_DOWNMEM_ROOT`
    - `GGML_DOWNMEM_LIB`

- Modify: `ggml/src/CMakeLists.txt`
  - Add `ggml_add_backend(Downmem)` after `ggml_add_backend(BLAS)` and before heavyweight GPU backends.

- Modify: `ggml/src/ggml-backend-reg.cpp`
  - Include `ggml-downmem.h` behind `#ifdef GGML_USE_DOWNMEM`.
  - Register `ggml_backend_downmem_reg()` before CPU registration.

- Create: `tests/test-downmem-backend.cpp`
  - ggml-level correctness test.
  - Runs CPU baseline graph and downmem-enabled graph on a small F32 GEMV.
  - Exits with a skip code when required env vars are missing so normal CI is unaffected.

- Modify: `tests/CMakeLists.txt`
  - Build `test-downmem-backend`.
  - Register it only when `GGML_DOWNMEM` is enabled.

- Create: `scripts/downmem-smoke.sh`
  - Builds downmem kernel if needed.
  - Configures/builds llama.cpp with `GGML_DOWNMEM=ON`.
  - Runs ggml test.
  - Runs a short `llama-cli` prompt with `/home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf`, `GGML_DOWNMEM_MAX_OPS=1`, and verbose logs.

---

## Contracts and Limits

- Supported op:
  - `op->op == GGML_OP_MUL_MAT`
  - `op->type == GGML_TYPE_F32`
  - `src1->type == GGML_TYPE_F32`
  - `src0->type == GGML_TYPE_F32` for the first passing path.
  - Quantized `src0` is allowed only after the F32 path passes and only when `GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1`; the host dequantizes rows to F32 before transfer and the DPU still runs the same F32 kernel.

- Supported shape:
  - `src0` is interpreted as `[K, N]`.
  - `src1` is interpreted as `[K, 1]`.
  - `dst` is interpreted as `[N, 1]`.
  - Reject batched/multi-column forms in the MVP:
    - `src1->ne[1] == 1`
    - `dst->ne[0] == src0->ne[1]`
    - `dst->ne[1] == 1`
    - `dst->nb[0] == sizeof(float)`
    - `dst->nb[1] >= dst->nb[0] * dst->ne[0]`
    - `src0->ne[2] == 1`
    - `src0->ne[3] == 1`
    - `src1->ne[2] == 1`
    - `src1->ne[3] == 1`
    - `dst->ne[2] == 1`
    - `dst->ne[3] == 1`

- MRAM layout per DPU:
  - `A`: offset `0`, size `max_rows * k_pad * sizeof(float)`
  - `x`: offset `a_bytes`, size `k_pad * sizeof(float)`
  - `y`: offset `a_bytes + x_bytes`, size `max_rows * sizeof(float)`

- Required environment variables:
  - `GGML_DOWNMEM=1` enables backend claiming.
  - `GGML_DOWNMEM_DPU_BIN=/home/fjg/src/downmem/build-rv/devApp/rvbins/LLAMA_GEMV_F32` points to the RV DPU binary.
  - `GGML_DOWNMEM_NR_DPUS=4` sets simulated DPU count.
  - `GGML_DOWNMEM_MAX_OPS=1` limits offload count during first llama smoke tests.
  - `GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1` is required for the local `stories15M-q4_0.gguf` end-to-end smoke because the available model has quantized weights.

- Optional environment variables:
  - `GGML_DOWNMEM_VALIDATE=1`
  - `GGML_DOWNMEM_VERBOSE=1`

- Runtime failure policy:
  - If an op was not scheduled to downmem, CPU handles it.
  - If an op was scheduled to downmem and DPU transfer/launch/validation fails, return `GGML_STATUS_FAILED`.
  - Do not silently recompute inside downmem backend after scheduler assignment.

---

## Task 1: Baseline and Branch Hygiene

**Files:**
- Read-only: `/home/fjg/src/downmem`
- Read-only: `/home/fjg/src/llama.cpp`

- [ ] **Step 1: Record downmem status**

Run:

```bash
cd /home/fjg/src/downmem
git status --short
git rev-parse --short HEAD
```

Expected output includes these pre-existing user changes:

```text
 M .gitignore
?? setup_downmem_upmem_env.sh
```

The commit hash should be at or after:

```text
827c70d
```

- [ ] **Step 2: Record llama.cpp status**

Run:

```bash
cd /home/fjg/src/llama.cpp
git status --short
git rev-parse --short HEAD
```

Expected:

```text
```

for `git status --short`, meaning no local modifications before this implementation starts.

- [ ] **Step 3: Verify existing downmem RV examples still work**

Run:

```bash
cd /home/fjg/src/downmem
DMM_NR_SIM_THRDS=4 ./build-rv/dmmGEMV 32 4 build-rv/devApp/rvbins/GEMV
DMM_NR_SIM_THRDS=4 ./build-rv/dmmOPDEMOF 1024 4 build-rv/devApp/rvbins/OPDEMOF 1
```

Expected:

```text
```

Both commands exit with status `0`.

- [ ] **Step 4: Commit nothing**

Run:

```bash
cd /home/fjg/src/downmem
git diff -- docs/superpowers/specs/2026-06-18-llama-downmem-offload-design.md
cd /home/fjg/src/llama.cpp
git status --short
```

Expected:

```text
```

No implementation changes have been made yet.

---

## Task 2: Downmem DPU Kernel Header Contract

**Files:**
- Create: `/home/fjg/src/downmem/devApp/LLAMA_GEMV_F32.c`

- [ ] **Step 1: Create the DPU source with argument contract and minimal no-op main**

Create `/home/fjg/src/downmem/devApp/LLAMA_GEMV_F32.c` with:

```c
#include "moredefs.h"
#include <barrier.h>
#include <mram.h>
#include <stdint.h>

typedef struct {
  uint32_t k;
  uint32_t k_pad;
  uint32_t n_rows;
  uint32_t max_rows;
  uint32_t a_offset;
  uint32_t x_offset;
  uint32_t y_offset;
} llama_gemv_f32_args_t;

__host llama_gemv_f32_args_t DPU_INPUT_ARGUMENTS;

ALL_THREADS_BARRIER_INIT();

int main(void) {
  all_threads_barrier_wait();
  return 0;
}
```

- [ ] **Step 2: Add RV target**

Modify `/home/fjg/src/downmem/devApp/CMakeLists.txt` inside the existing `if(DMM_RV)` block, after the `foreach(O BFS ... VA)` block:

```cmake
  add_executable(rvLLAMA_GEMV_F32 LLAMA_GEMV_F32.c)
  rvbin_make(rvLLAMA_GEMV_F32 16 -flto -O3)
  add_dependencies(dpuExamples rvLLAMA_GEMV_F32)
```

- [ ] **Step 3: Build only the new RV binary**

Run:

```bash
cd /home/fjg/src/downmem
cmake --build build-rv --target rvLLAMA_GEMV_F32 -j"$(nproc)"
```

Expected includes:

```text
Built target rvLLAMA_GEMV_F32
```

- [ ] **Step 4: Verify binary path**

Run:

```bash
cd /home/fjg/src/downmem
test -x build-rv/devApp/rvbins/LLAMA_GEMV_F32
```

Expected: exit status `0`.

- [ ] **Step 5: Commit kernel scaffold**

Run:

```bash
cd /home/fjg/src/downmem
git add devApp/LLAMA_GEMV_F32.c devApp/CMakeLists.txt
git commit -m "Add llama GEMV F32 DPU kernel scaffold"
```

Expected:

```text
[... Add llama GEMV F32 DPU kernel scaffold]
```

---

## Task 3: Downmem Host GEMV Test Scaffold

**Files:**
- Create: `/home/fjg/src/downmem/hostApp/LLAMA_GEMV_F32.c`
- Modify: `/home/fjg/src/downmem/CMakeLists.txt`

- [ ] **Step 1: Create host test source with deterministic cases and a temporary launch-only path**

Create `/home/fjg/src/downmem/hostApp/LLAMA_GEMV_F32.c` with:

```c
#include <assert.h>
#include <dpu.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t k;
  uint32_t k_pad;
  uint32_t n_rows;
  uint32_t max_rows;
  uint32_t a_offset;
  uint32_t x_offset;
  uint32_t y_offset;
} llama_gemv_f32_args_t;

typedef struct {
  uint32_t rows;
  uint32_t row_offset;
} dpu_rows_t;

static uint32_t align_up_u32(uint32_t v, uint32_t a) {
  return ((v + a - 1) / a) * a;
}

static float pattern_a(uint32_t row, uint32_t col) {
  const int v = (int)((row * 17u + col * 13u + 7u) % 23u) - 11;
  return (float)v * 0.03125f;
}

static float pattern_x(uint32_t col) {
  const int v = (int)((col * 19u + 5u) % 29u) - 14;
  return (float)v * 0.0625f;
}

static void host_gemv(const float * a, const float * x, float * y, uint32_t n, uint32_t k) {
  for (uint32_t row = 0; row < n; ++row) {
    float sum = 0.0f;
    for (uint32_t col = 0; col < k; ++col) {
      sum += a[row * k + col] * x[col];
    }
    y[row] = sum;
  }
}

static int nearly_equal(float got, float expected) {
  const float diff = fabsf(got - expected);
  const float scale = fmaxf(1.0f, fabsf(expected));
  return diff <= 1.0e-4f || diff <= scale * 1.0e-4f;
}

static int run_case(uint32_t n, uint32_t k, uint32_t nr_dpus, const char * dpu_binary) {
  struct dpu_set_t set;

  DMM_VERIFY(dpu_alloc(nr_dpus, NULL, &set));
  DMM_VERIFY(dpu_load(set, dpu_binary, NULL));

  uint32_t actual_dpus = 0;
  DMM_VERIFY(dpu_get_nr_dpus(set, &actual_dpus));
  assert(actual_dpus == nr_dpus);

  const uint32_t k_pad = align_up_u32(k, 2);
  uint32_t max_rows = 0;
  dpu_rows_t * layout = calloc(nr_dpus, sizeof(*layout));
  llama_gemv_f32_args_t * args = calloc(nr_dpus, sizeof(*args));
  assert(layout != NULL && args != NULL);

  for (uint32_t i = 0; i < nr_dpus; ++i) {
    const uint32_t base = n / nr_dpus;
    const uint32_t rest = n % nr_dpus;
    layout[i].rows = base + (i < rest ? 1u : 0u);
    layout[i].row_offset = i * base + (i < rest ? i : rest);
    if (layout[i].rows > max_rows) {
      max_rows = layout[i].rows;
    }
  }
  if (max_rows == 0) {
    max_rows = 1;
  }

  const uint32_t a_floats_per_dpu = max_rows * k_pad;
  const uint32_t x_floats_per_dpu = k_pad;
  const uint32_t y_floats_per_dpu = max_rows;
  const uint32_t a_bytes = a_floats_per_dpu * sizeof(float);
  const uint32_t x_bytes = x_floats_per_dpu * sizeof(float);
  const uint32_t y_bytes = y_floats_per_dpu * sizeof(float);

  float * a = calloc((size_t)n * k, sizeof(float));
  float * x = calloc(k_pad, sizeof(float));
  float * y_ref = calloc(n, sizeof(float));
  float * y_dpu_padded = calloc((size_t)nr_dpus * max_rows, sizeof(float));
  float * a_padded = calloc((size_t)nr_dpus * a_floats_per_dpu, sizeof(float));
  assert(a != NULL && x != NULL && y_ref != NULL && y_dpu_padded != NULL && a_padded != NULL);

  for (uint32_t row = 0; row < n; ++row) {
    for (uint32_t col = 0; col < k; ++col) {
      a[row * k + col] = pattern_a(row, col);
    }
  }
  for (uint32_t col = 0; col < k; ++col) {
    x[col] = pattern_x(col);
  }
  host_gemv(a, x, y_ref, n, k);

  for (uint32_t d = 0; d < nr_dpus; ++d) {
    for (uint32_t row = 0; row < layout[d].rows; ++row) {
      const uint32_t global_row = layout[d].row_offset + row;
      memcpy(a_padded + (size_t)d * a_floats_per_dpu + row * k_pad,
             a + (size_t)global_row * k,
             k * sizeof(float));
    }
    args[d] = (llama_gemv_f32_args_t) {
      .k = k,
      .k_pad = k_pad,
      .n_rows = layout[d].rows,
      .max_rows = max_rows,
      .a_offset = 0,
      .x_offset = a_bytes,
      .y_offset = a_bytes + x_bytes,
    };
  }

  struct dpu_set_t each;
  uint32_t idx = 0;
  DPU_FOREACH(set, each, idx) {
    DMM_VERIFY(dpu_prepare_xfer(each, args + idx));
  }
  DMM_VERIFY(dpu_push_xfer(set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0,
                           sizeof(llama_gemv_f32_args_t), DPU_XFER_DEFAULT));

  idx = 0;
  DPU_FOREACH(set, each, idx) {
    DMM_VERIFY(dpu_prepare_xfer(each, a_padded + (size_t)idx * a_floats_per_dpu));
  }
  DMM_VERIFY(dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                           a_bytes, DPU_XFER_DEFAULT));

  DMM_VERIFY(dpu_broadcast_to(set, DPU_MRAM_HEAP_POINTER_NAME, a_bytes, x,
                              x_bytes, DPU_XFER_DEFAULT));

  DMM_VERIFY(dpu_launch(set, DPU_SYNCHRONOUS));

  idx = 0;
  DPU_FOREACH(set, each, idx) {
    DMM_VERIFY(dpu_prepare_xfer(each, y_dpu_padded + (size_t)idx * max_rows));
  }
  DMM_VERIFY(dpu_push_xfer(set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, a_bytes + x_bytes,
                           y_bytes, DPU_XFER_DEFAULT));

  int ok = 1;
  for (uint32_t d = 0; d < nr_dpus; ++d) {
    for (uint32_t row = 0; row < layout[d].rows; ++row) {
      const uint32_t global_row = layout[d].row_offset + row;
      const float got = y_dpu_padded[(size_t)d * max_rows + row];
      const float expected = y_ref[global_row];
      if (!nearly_equal(got, expected)) {
        fprintf(stderr, "case n=%u k=%u dpus=%u row=%u expected=%g got=%g\n",
                n, k, nr_dpus, global_row, expected, got);
        ok = 0;
        goto done;
      }
    }
  }

done:
  free(a);
  free(x);
  free(y_ref);
  free(y_dpu_padded);
  free(a_padded);
  free(layout);
  free(args);
  DMM_VERIFY(dpu_free(set));
  return ok ? 0 : 1;
}

int main(int argc, char ** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s /path/to/LLAMA_GEMV_F32\n", argv[0]);
    return 2;
  }

  const char * dpu_binary = argv[1];
  int failed = 0;
  failed |= run_case(8, 16, 1, dpu_binary);
  failed |= run_case(17, 31, 4, dpu_binary);
  failed |= run_case(33, 64, 6, dpu_binary);
  failed |= run_case(3, 7, 8, dpu_binary);

  if (failed != 0) {
    fprintf(stderr, "LLAMA_GEMV_F32 failed\n");
    return 1;
  }

  printf("LLAMA_GEMV_F32 passed\n");
  return 0;
}
```

- [ ] **Step 2: Add host executable to CMake**

Modify `/home/fjg/src/downmem/CMakeLists.txt` after the `foreach(A BS COMPACT ... VA-SIMPLE)` block:

```cmake
add_executable(dmmLLAMA_GEMV_F32 hostApp/LLAMA_GEMV_F32.c)
target_link_libraries(dmmLLAMA_GEMV_F32 PRIVATE dmm m)
```

- [ ] **Step 3: Build host test**

Run:

```bash
cd /home/fjg/src/downmem
cmake --build build-rv --target dmmLLAMA_GEMV_F32 -j"$(nproc)"
```

Expected includes:

```text
Built target dmmLLAMA_GEMV_F32
```

- [ ] **Step 4: Run test and verify it fails against no-op kernel**

Run:

```bash
cd /home/fjg/src/downmem
DMM_NR_SIM_THRDS=4 ./build-rv/dmmLLAMA_GEMV_F32 build-rv/devApp/rvbins/LLAMA_GEMV_F32
```

Expected:

```text
case n=8 k=16 dpus=1 row=0 expected=
LLAMA_GEMV_F32 failed
```

Exit status must be `1`.

- [ ] **Step 5: Commit failing host test**

Run:

```bash
cd /home/fjg/src/downmem
git add hostApp/LLAMA_GEMV_F32.c CMakeLists.txt
git commit -m "Add failing llama GEMV F32 host test"
```

Expected:

```text
[... Add failing llama GEMV F32 host test]
```

---

## Task 4: Implement Downmem DPU GEMV F32

**Files:**
- Modify: `/home/fjg/src/downmem/devApp/LLAMA_GEMV_F32.c`

- [ ] **Step 1: Replace no-op DPU main with GEMV implementation**

Replace the contents of `/home/fjg/src/downmem/devApp/LLAMA_GEMV_F32.c` with:

```c
#include "moredefs.h"
#include <barrier.h>
#include <mram.h>
#include <stdint.h>

typedef struct {
  uint32_t k;
  uint32_t k_pad;
  uint32_t n_rows;
  uint32_t max_rows;
  uint32_t a_offset;
  uint32_t x_offset;
  uint32_t y_offset;
} llama_gemv_f32_args_t;

__host llama_gemv_f32_args_t DPU_INPUT_ARGUMENTS;

#define LLAMA_GEMV_CHUNK_FLOATS 64u
#define LLAMA_GEMV_CHUNK_BYTES (LLAMA_GEMV_CHUNK_FLOATS * sizeof(float))

ALL_THREADS_BARRIER_INIT();

static uint32_t rows_start_for_tasklet(uint32_t tasklet_id, uint32_t n_rows) {
  const uint32_t base = n_rows / NR_TASKLETS;
  const uint32_t rest = n_rows % NR_TASKLETS;
  return tasklet_id * base + (tasklet_id < rest ? tasklet_id : rest);
}

static uint32_t rows_count_for_tasklet(uint32_t tasklet_id, uint32_t n_rows) {
  const uint32_t base = n_rows / NR_TASKLETS;
  const uint32_t rest = n_rows % NR_TASKLETS;
  return base + (tasklet_id < rest ? 1u : 0u);
}

int main(void) {
  const uint32_t tasklet_id = me();
  const uint32_t k = DPU_INPUT_ARGUMENTS.k;
  const uint32_t k_pad = DPU_INPUT_ARGUMENTS.k_pad;
  const uint32_t n_rows = DPU_INPUT_ARGUMENTS.n_rows;
  const uint32_t a_offset = DPU_INPUT_ARGUMENTS.a_offset;
  const uint32_t x_offset = DPU_INPUT_ARGUMENTS.x_offset;
  const uint32_t y_offset = DPU_INPUT_ARGUMENTS.y_offset;

  float a_buf[LLAMA_GEMV_CHUNK_FLOATS];
  float x_buf[LLAMA_GEMV_CHUNK_FLOATS];
  float y_buf[1];

  all_threads_barrier_wait();

  const uint32_t row_start = rows_start_for_tasklet(tasklet_id, n_rows);
  const uint32_t row_count = rows_count_for_tasklet(tasklet_id, n_rows);
  const uint32_t row_end = row_start + row_count;

  for (uint32_t row = row_start; row < row_end; ++row) {
    float sum = 0.0f;

    for (uint32_t col = 0; col < k; col += LLAMA_GEMV_CHUNK_FLOATS) {
      uint32_t chunk = k - col;
      if (chunk > LLAMA_GEMV_CHUNK_FLOATS) {
        chunk = LLAMA_GEMV_CHUNK_FLOATS;
      }

      const uint32_t bytes = chunk * sizeof(float);
      const uintptr_t a_addr = (uintptr_t)DPU_MRAM_HEAP_POINTER +
                               a_offset + (row * k_pad + col) * sizeof(float);
      const uintptr_t x_addr = (uintptr_t)DPU_MRAM_HEAP_POINTER +
                               x_offset + col * sizeof(float);

      mram_read((__mram_ptr void const *)a_addr, a_buf, bytes);
      mram_read((__mram_ptr void const *)x_addr, x_buf, bytes);

      for (uint32_t i = 0; i < chunk; ++i) {
        sum += a_buf[i] * x_buf[i];
      }
    }

    y_buf[0] = sum;
    const uintptr_t y_addr = (uintptr_t)DPU_MRAM_HEAP_POINTER +
                             y_offset + row * sizeof(float);
    mram_write(y_buf, (__mram_ptr void *)y_addr, sizeof(float));
  }

  return 0;
}
```

- [ ] **Step 2: Build kernel and host test**

Run:

```bash
cd /home/fjg/src/downmem
cmake --build build-rv --target rvLLAMA_GEMV_F32 dmmLLAMA_GEMV_F32 -j"$(nproc)"
```

Expected includes:

```text
Built target rvLLAMA_GEMV_F32
Built target dmmLLAMA_GEMV_F32
```

- [ ] **Step 3: Run deterministic host test**

Run:

```bash
cd /home/fjg/src/downmem
DMM_NR_SIM_THRDS=4 ./build-rv/dmmLLAMA_GEMV_F32 build-rv/devApp/rvbins/LLAMA_GEMV_F32
```

Expected:

```text
LLAMA_GEMV_F32 passed
```

- [ ] **Step 4: Run with one simulator thread**

Run:

```bash
cd /home/fjg/src/downmem
DMM_NR_SIM_THRDS=1 ./build-rv/dmmLLAMA_GEMV_F32 build-rv/devApp/rvbins/LLAMA_GEMV_F32
```

Expected:

```text
LLAMA_GEMV_F32 passed
```

- [ ] **Step 5: Commit passing DPU GEMV**

Run:

```bash
cd /home/fjg/src/downmem
git add devApp/LLAMA_GEMV_F32.c
git commit -m "Implement llama GEMV F32 DPU kernel"
```

Expected:

```text
[... Implement llama GEMV F32 DPU kernel]
```

---

## Task 5: Add Downmem Smoke Script

**Files:**
- Create: `/home/fjg/src/downmem/scripts/run-llama-gemv-f32-test.sh`

- [ ] **Step 1: Create script**

Create `/home/fjg/src/downmem/scripts/run-llama-gemv-f32-test.sh` with:

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-rv"
DPU_BIN="${BUILD_DIR}/devApp/rvbins/LLAMA_GEMV_F32"

cmake --build "${BUILD_DIR}" --target rvLLAMA_GEMV_F32 dmmLLAMA_GEMV_F32 -j"$(nproc)"

if [[ ! -x "${DPU_BIN}" ]]; then
  echo "missing DPU binary: ${DPU_BIN}" >&2
  exit 1
fi

DMM_NR_SIM_THRDS="${DMM_NR_SIM_THRDS:-4}" \
  "${BUILD_DIR}/dmmLLAMA_GEMV_F32" "${DPU_BIN}"
```

- [ ] **Step 2: Make script executable**

Run:

```bash
cd /home/fjg/src/downmem
chmod +x scripts/run-llama-gemv-f32-test.sh
```

- [ ] **Step 3: Run script**

Run:

```bash
cd /home/fjg/src/downmem
scripts/run-llama-gemv-f32-test.sh
```

Expected:

```text
LLAMA_GEMV_F32 passed
```

- [ ] **Step 4: Commit script**

Run:

```bash
cd /home/fjg/src/downmem
git add scripts/run-llama-gemv-f32-test.sh
git commit -m "Add llama GEMV F32 downmem smoke script"
```

Expected:

```text
[... Add llama GEMV F32 downmem smoke script]
```

---

## Task 6: llama.cpp CMake and Backend Registration Scaffold

**Files:**
- Create: `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/ggml-downmem.h`
- Create: `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/ggml-downmem.cpp`
- Create: `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/CMakeLists.txt`
- Modify: `/home/fjg/src/llama.cpp/ggml/CMakeLists.txt`
- Modify: `/home/fjg/src/llama.cpp/ggml/src/CMakeLists.txt`
- Modify: `/home/fjg/src/llama.cpp/ggml/src/ggml-backend-reg.cpp`

- [ ] **Step 1: Add backend header**

Create `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/ggml-downmem.h` with:

```c
#pragma once

#include "ggml-backend.h"

#ifdef  __cplusplus
extern "C" {
#endif

GGML_BACKEND_API ggml_backend_reg_t ggml_backend_downmem_reg(void);

#ifdef  __cplusplus
}
#endif
```

- [ ] **Step 2: Add minimal backend implementation that registers but supports no ops**

Create `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/ggml-downmem.cpp` with:

```cpp
#include "ggml-downmem.h"

#include "ggml-backend-impl.h"
#include "ggml-impl.h"
#include "ggml.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

struct ggml_backend_downmem_context {
    int placeholder = 0;
};

static bool ggml_downmem_env_enabled(void) {
    const char * value = std::getenv("GGML_DOWNMEM");
    return value != nullptr && std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
}

static bool ggml_downmem_file_exists(const char * path) {
    if (path == nullptr) {
        return false;
    }
    FILE * file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
    return true;
}

static const char * ggml_backend_downmem_get_name(ggml_backend_t backend) {
    GGML_UNUSED(backend);
    return "Downmem";
}

static void ggml_backend_downmem_free(ggml_backend_t backend) {
    delete (ggml_backend_downmem_context *) backend->context;
    delete backend;
}

static enum ggml_status ggml_backend_downmem_graph_compute(ggml_backend_t backend, struct ggml_cgraph * cgraph) {
    GGML_UNUSED(backend);
    GGML_UNUSED(cgraph);
    return GGML_STATUS_SUCCESS;
}

static const ggml_backend_i ggml_backend_downmem_i = {
    /* .get_name                = */ ggml_backend_downmem_get_name,
    /* .free                    = */ ggml_backend_downmem_free,
    /* .set_tensor_async        = */ nullptr,
    /* .get_tensor_async        = */ nullptr,
    /* .set_tensor_2d_async     = */ nullptr,
    /* .get_tensor_2d_async     = */ nullptr,
    /* .cpy_tensor_async        = */ nullptr,
    /* .synchronize             = */ nullptr,
    /* .graph_plan_create       = */ nullptr,
    /* .graph_plan_free         = */ nullptr,
    /* .graph_plan_update       = */ nullptr,
    /* .graph_plan_compute      = */ nullptr,
    /* .graph_compute           = */ ggml_backend_downmem_graph_compute,
    /* .event_record            = */ nullptr,
    /* .event_wait              = */ nullptr,
    /* .graph_optimize          = */ nullptr,
};

static ggml_backend_t ggml_backend_downmem_init(ggml_backend_dev_t dev, const char * params) {
    GGML_UNUSED(params);

    ggml_backend_t backend = new ggml_backend {
        /* .guid      = */ nullptr,
        /* .iface     = */ ggml_backend_downmem_i,
        /* .device    = */ dev,
        /* .context   = */ new ggml_backend_downmem_context,
    };

    return backend;
}

static const char * ggml_backend_downmem_device_get_name(ggml_backend_dev_t dev) {
    GGML_UNUSED(dev);
    return "Downmem";
}

static const char * ggml_backend_downmem_device_get_description(ggml_backend_dev_t dev) {
    GGML_UNUSED(dev);
    return "Downmem RV simulator";
}

static void ggml_backend_downmem_device_get_memory(ggml_backend_dev_t dev, size_t * free, size_t * total) {
    GGML_UNUSED(dev);
    *free = 64ull * 1024ull * 1024ull;
    *total = 64ull * 1024ull * 1024ull;
}

static enum ggml_backend_dev_type ggml_backend_downmem_device_get_type(ggml_backend_dev_t dev) {
    GGML_UNUSED(dev);
    return GGML_BACKEND_DEVICE_TYPE_ACCEL;
}

static void ggml_backend_downmem_device_get_props(ggml_backend_dev_t dev, struct ggml_backend_dev_props * props) {
    props->name = ggml_backend_downmem_device_get_name(dev);
    props->description = ggml_backend_downmem_device_get_description(dev);
    props->type = ggml_backend_downmem_device_get_type(dev);
    props->device_id = nullptr;
    ggml_backend_downmem_device_get_memory(dev, &props->memory_free, &props->memory_total);
    props->caps = {
        /* .async                 = */ false,
        /* .host_buffer           = */ true,
        /* .buffer_from_host_ptr  = */ false,
        /* .events                = */ false,
    };
}

static ggml_backend_t ggml_backend_downmem_device_init(ggml_backend_dev_t dev, const char * params) {
    return ggml_backend_downmem_init(dev, params);
}

static ggml_backend_buffer_type_t ggml_backend_downmem_device_get_buffer_type(ggml_backend_dev_t dev) {
    GGML_UNUSED(dev);
    return ggml_backend_cpu_buffer_type();
}

static bool ggml_backend_downmem_device_supports_op(ggml_backend_dev_t dev, const struct ggml_tensor * op) {
    GGML_UNUSED(dev);
    GGML_UNUSED(op);
    return false;
}

static bool ggml_backend_downmem_device_offload_op(ggml_backend_dev_t dev, const struct ggml_tensor * op) {
    GGML_UNUSED(dev);
    GGML_UNUSED(op);
    return ggml_downmem_env_enabled() && false;
}

static const ggml_backend_device_i ggml_backend_downmem_device_i = {
    /* .get_name             = */ ggml_backend_downmem_device_get_name,
    /* .get_description      = */ ggml_backend_downmem_device_get_description,
    /* .get_memory           = */ ggml_backend_downmem_device_get_memory,
    /* .get_type             = */ ggml_backend_downmem_device_get_type,
    /* .get_props            = */ ggml_backend_downmem_device_get_props,
    /* .init_backend         = */ ggml_backend_downmem_device_init,
    /* .get_buffer_type      = */ ggml_backend_downmem_device_get_buffer_type,
    /* .get_host_buffer_type = */ nullptr,
    /* .buffer_from_host_ptr = */ nullptr,
    /* .supports_op          = */ ggml_backend_downmem_device_supports_op,
    /* .supports_buft        = */ nullptr,
    /* .offload_op           = */ ggml_backend_downmem_device_offload_op,
    /* .event_new            = */ nullptr,
    /* .event_free           = */ nullptr,
    /* .event_synchronize    = */ nullptr,
};

struct ggml_backend_downmem_reg_context {
    ggml_backend_device device;
};

static const char * ggml_backend_downmem_reg_get_name(ggml_backend_reg_t reg) {
    GGML_UNUSED(reg);
    return "Downmem";
}

static size_t ggml_backend_downmem_reg_get_device_count(ggml_backend_reg_t reg) {
    GGML_UNUSED(reg);
    return 1;
}

static ggml_backend_dev_t ggml_backend_downmem_reg_get_device(ggml_backend_reg_t reg, size_t index) {
    GGML_ASSERT(index == 0);
    auto * ctx = (ggml_backend_downmem_reg_context *) reg->context;
    return &ctx->device;
}

static const ggml_backend_reg_i ggml_backend_downmem_reg_i = {
    /* .get_name         = */ ggml_backend_downmem_reg_get_name,
    /* .get_device_count = */ ggml_backend_downmem_reg_get_device_count,
    /* .get_device       = */ ggml_backend_downmem_reg_get_device,
    /* .get_proc_address = */ nullptr,
};

ggml_backend_reg_t ggml_backend_downmem_reg(void) {
    static ggml_backend_downmem_reg_context ctx = {
        /* .device = */ {
            /* .iface   = */ ggml_backend_downmem_device_i,
            /* .reg     = */ nullptr,
            /* .context = */ nullptr,
        },
    };

    static ggml_backend_reg reg = {
        /* .api_version = */ GGML_BACKEND_API_VERSION,
        /* .iface       = */ ggml_backend_downmem_reg_i,
        /* .context     = */ &ctx,
    };

    ctx.device.reg = &reg;
    return &reg;
}
```

- [ ] **Step 3: Add backend CMake**

Create `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/CMakeLists.txt` with:

```cmake
if (NOT GGML_DOWNMEM_ROOT)
    set(GGML_DOWNMEM_ROOT "/home/fjg/src/downmem" CACHE PATH "ggml: downmem source root")
endif()

if (NOT GGML_DOWNMEM_LIB)
    set(GGML_DOWNMEM_LIB "${GGML_DOWNMEM_ROOT}/build-rv/libdmm.a" CACHE FILEPATH "ggml: downmem dmm library")
endif()

find_package(OpenMP REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCRE2 REQUIRED libpcre2-8)

ggml_add_backend_library(ggml-downmem
                         ggml-downmem.cpp
                         ggml-downmem.h)

target_include_directories(ggml-downmem PRIVATE
    ${GGML_DOWNMEM_ROOT}
    ${PCRE2_INCLUDE_DIRS})

target_link_libraries(ggml-downmem PRIVATE
    ${GGML_DOWNMEM_LIB}
    OpenMP::OpenMP_C
    OpenMP::OpenMP_CXX
    ${PCRE2_LIBRARIES}
    elf
    m)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(ggml-downmem PRIVATE numa)
endif()
```

- [ ] **Step 4: Add ggml options**

Modify `/home/fjg/src/llama.cpp/ggml/CMakeLists.txt` near the other backend options, after `option(GGML_BLAS ...)`:

```cmake
option(GGML_DOWNMEM "ggml: use downmem simulator backend" OFF)
set(GGML_DOWNMEM_ROOT "/home/fjg/src/downmem" CACHE PATH "ggml: downmem source root")
set(GGML_DOWNMEM_LIB "${GGML_DOWNMEM_ROOT}/build-rv/libdmm.a" CACHE FILEPATH "ggml: downmem dmm library")
```

- [ ] **Step 5: Add backend to source CMake**

Modify `/home/fjg/src/llama.cpp/ggml/src/CMakeLists.txt` near the backend list:

```cmake
ggml_add_backend(BLAS)
ggml_add_backend(Downmem)
ggml_add_backend(CANN)
```

- [ ] **Step 6: Register backend**

Modify `/home/fjg/src/llama.cpp/ggml/src/ggml-backend-reg.cpp`.

Add after the BLAS include:

```cpp
#ifdef GGML_USE_DOWNMEM
#include "ggml-downmem/ggml-downmem.h"
#endif
```

Add after BLAS registration and before RPC/OpenVINO/CPU registration:

```cpp
#ifdef GGML_USE_DOWNMEM
        register_backend(ggml_backend_downmem_reg());
#endif
```

- [ ] **Step 7: Configure llama.cpp with Downmem enabled**

Run:

```bash
cd /home/fjg/src/llama.cpp
cmake -S . -B build-downmem \
  -DGGML_DOWNMEM=ON \
  -DGGML_DOWNMEM_ROOT=/home/fjg/src/downmem \
  -DGGML_DOWNMEM_LIB=/home/fjg/src/downmem/build-rv/libdmm.a \
  -DLLAMA_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
```

Expected includes:

```text
Including Downmem backend
```

- [ ] **Step 8: Build ggml and llama-cli**

Run:

```bash
cd /home/fjg/src/llama.cpp
cmake --build build-downmem --target ggml llama-cli -j"$(nproc)"
```

Expected includes:

```text
Built target ggml-downmem
Built target ggml
Built target llama-cli
```

- [ ] **Step 9: Verify default behavior does not require env vars**

Run:

```bash
cd /home/fjg/src/llama.cpp
./build-downmem/bin/llama-cli \
  -m /home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf \
  -p "Once upon a time" \
  -n 1 \
  -t 1 \
  --no-display-prompt
```

Expected:

```text
```

Command exits `0`. No `Downmem` verbose offload logs appear because `GGML_DOWNMEM` is unset.

- [ ] **Step 10: Commit scaffold**

Run:

```bash
cd /home/fjg/src/llama.cpp
git add ggml/src/ggml-downmem ggml/CMakeLists.txt ggml/src/CMakeLists.txt ggml/src/ggml-backend-reg.cpp
git commit -m "Add downmem ggml backend scaffold"
```

Expected:

```text
[... Add downmem ggml backend scaffold]
```

---

## Task 7: Implement llama.cpp Downmem Shape Gating

**Files:**
- Modify: `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/ggml-downmem.cpp`

- [ ] **Step 1: Add environment parsing and supported-op predicate**

Modify `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/ggml-downmem.cpp` by adding these helpers after `ggml_downmem_env_enabled`:

```cpp
static int ggml_downmem_env_i32(const char * name, int default_value) {
    const char * value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }
    char * end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || parsed < 0 || parsed > 1L << 30) {
        return default_value;
    }
    return (int) parsed;
}

static const char * ggml_downmem_dpu_bin(void) {
    const char * value = std::getenv("GGML_DOWNMEM_DPU_BIN");
    return value != nullptr && value[0] != '\0' ? value : nullptr;
}

static bool ggml_downmem_verbose(void) {
    const char * value = std::getenv("GGML_DOWNMEM_VERBOSE");
    return value != nullptr && std::strcmp(value, "0") != 0;
}

static bool ggml_downmem_tensor_is_plain_f32_1d_or_2d(const ggml_tensor * t) {
    return t != nullptr &&
           t->type == GGML_TYPE_F32 &&
           t->nb[0] == (int64_t) sizeof(float) &&
           t->nb[1] >= t->nb[0] * t->ne[0];
}

static bool ggml_downmem_supports_mul_mat_f32_gemv(const ggml_tensor * op, bool require_runtime_ready) {
    if (!ggml_downmem_env_enabled()) {
        return false;
    }
    if (require_runtime_ready && !ggml_downmem_file_exists(ggml_downmem_dpu_bin())) {
        return false;
    }
    if (op == nullptr || op->op != GGML_OP_MUL_MAT || op->type != GGML_TYPE_F32) {
        return false;
    }
    const ggml_tensor * src0 = op->src[0];
    const ggml_tensor * src1 = op->src[1];
    if (!ggml_downmem_tensor_is_plain_f32_1d_or_2d(src0) ||
        !ggml_downmem_tensor_is_plain_f32_1d_or_2d(src1)) {
        return false;
    }
    if (src0->ne[0] != src1->ne[0]) {
        return false;
    }
    if (src1->ne[1] != 1 || op->ne[0] != src0->ne[1] || op->ne[1] != 1) {
        return false;
    }
    if (op->nb[0] != (int64_t) sizeof(float) || op->nb[1] < op->nb[0] * op->ne[0]) {
        return false;
    }
    if (src0->ne[2] != 1 || src0->ne[3] != 1 ||
        src1->ne[2] != 1 || src1->ne[3] != 1 ||
        op->ne[2] != 1 || op->ne[3] != 1) {
        return false;
    }
    return true;
}
```

- [ ] **Step 2: Replace supports/offload stubs**

Replace `ggml_backend_downmem_device_supports_op` with:

```cpp
static bool ggml_backend_downmem_device_supports_op(ggml_backend_dev_t dev, const struct ggml_tensor * op) {
    GGML_UNUSED(dev);
    return ggml_downmem_supports_mul_mat_f32_gemv(op, false);
}
```

Replace `ggml_backend_downmem_device_offload_op` with:

```cpp
static bool ggml_backend_downmem_device_offload_op(ggml_backend_dev_t dev, const struct ggml_tensor * op) {
    GGML_UNUSED(dev);

    static int claimed = 0;
    const int max_ops = ggml_downmem_env_i32("GGML_DOWNMEM_MAX_OPS", 1 << 30);
    const bool supported = ggml_downmem_supports_mul_mat_f32_gemv(op, true);

    if (!supported || claimed >= max_ops) {
        return false;
    }

    ++claimed;
    if (ggml_downmem_verbose()) {
        GGML_LOG_INFO("%s: claiming MUL_MAT k=%" PRId64 " n=%" PRId64 " claimed=%d max=%d\n",
                      __func__, op->src[0]->ne[0], op->src[0]->ne[1], claimed, max_ops);
    }
    return true;
}
```

Also add `#include <cinttypes>` at the top because this step uses `PRId64`.

- [ ] **Step 3: Build**

Run:

```bash
cd /home/fjg/src/llama.cpp
cmake --build build-downmem --target ggml llama-cli -j"$(nproc)"
```

Expected:

```text
Built target ggml-downmem
Built target ggml
Built target llama-cli
```

- [ ] **Step 4: Run default llama smoke**

Run:

```bash
cd /home/fjg/src/llama.cpp
./build-downmem/bin/llama-cli \
  -m /home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf \
  -p "Once upon a time" \
  -n 1 \
  -t 1 \
  --no-display-prompt
```

Expected: exits `0`, no downmem offload log.

- [ ] **Step 5: Run enabled mode with invalid binary and verify no crash**

Run:

```bash
cd /home/fjg/src/llama.cpp
GGML_DOWNMEM=1 \
GGML_DOWNMEM_DPU_BIN=/does/not/exist \
GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1 \
GGML_DOWNMEM_VERBOSE=1 \
./build-downmem/bin/llama-cli \
  -m /home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf \
  -p "Once upon a time" \
  -n 1 \
  -t 1 \
  --no-display-prompt
```

Expected: exits `0`. No `claiming MUL_MAT` log appears because `GGML_DOWNMEM_DPU_BIN` is invalid or missing from the runtime-ready path.

- [ ] **Step 6: Commit shape gating**

Run:

```bash
cd /home/fjg/src/llama.cpp
git add ggml/src/ggml-downmem/ggml-downmem.cpp
git commit -m "Gate downmem backend to F32 GEMV mul_mat"
```

Expected:

```text
[... Gate downmem backend to F32 GEMV mul_mat]
```

---

## Task 8: Implement llama.cpp Downmem Host Bridge

**Files:**
- Modify: `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/ggml-downmem.cpp`

- [ ] **Step 1: Add downmem API includes and bridge structs**

Add near the top of `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/ggml-downmem.cpp`:

```cpp
extern "C" {
#include <dpu.h>
}

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
```

Replace `ggml_backend_downmem_context` with:

```cpp
struct llama_gemv_f32_args_t {
    uint32_t k;
    uint32_t k_pad;
    uint32_t n_rows;
    uint32_t max_rows;
    uint32_t a_offset;
    uint32_t x_offset;
    uint32_t y_offset;
};

struct dpu_rows_t {
    uint32_t rows;
    uint32_t row_offset;
};

struct ggml_backend_downmem_context {
    bool loaded = false;
    bool failed = false;
    uint32_t nr_dpus = 0;
    std::string binary_path;
    dpu_set_t set = {};
    int executed_ops = 0;
};
```

- [ ] **Step 2: Add bridge helpers**

Add after the supported-op predicate:

```cpp
static uint32_t ggml_downmem_align_up_u32(uint32_t v, uint32_t a) {
    return ((v + a - 1u) / a) * a;
}

static bool ggml_downmem_load(ggml_backend_downmem_context * ctx) {
    if (ctx->loaded) {
        return true;
    }
    if (ctx->failed) {
        return false;
    }

    const char * bin = ggml_downmem_dpu_bin();
    if (bin == nullptr) {
        ctx->failed = true;
        return false;
    }

    ctx->nr_dpus = (uint32_t) ggml_downmem_env_i32("GGML_DOWNMEM_NR_DPUS", 4);
    if (ctx->nr_dpus == 0) {
        ctx->nr_dpus = 1;
    }
    ctx->binary_path = bin;

    if (dpu_alloc(ctx->nr_dpus, nullptr, &ctx->set) != DPU_OK) {
        GGML_LOG_ERROR("%s: dpu_alloc failed for %u DPUs\n", __func__, ctx->nr_dpus);
        ctx->failed = true;
        return false;
    }
    if (dpu_load(ctx->set, ctx->binary_path.c_str(), nullptr) != DPU_OK) {
        GGML_LOG_ERROR("%s: dpu_load failed for %s\n", __func__, ctx->binary_path.c_str());
        dpu_free(ctx->set);
        ctx->failed = true;
        return false;
    }

    uint32_t actual = 0;
    if (dpu_get_nr_dpus(ctx->set, &actual) != DPU_OK || actual == 0) {
        GGML_LOG_ERROR("%s: dpu_get_nr_dpus failed\n", __func__);
        dpu_free(ctx->set);
        ctx->failed = true;
        return false;
    }
    ctx->nr_dpus = actual;
    ctx->loaded = true;
    return true;
}

static void ggml_downmem_stage_src0_row_major(const ggml_tensor * src0, float * a_rows, uint32_t k, uint32_t n, uint32_t k_pad) {
    for (uint32_t row = 0; row < n; ++row) {
        const char * src_row = (const char *) src0->data + row * src0->nb[1];
        std::memcpy(a_rows + (size_t) row * k_pad, src_row, k * sizeof(float));
    }
}

static void ggml_downmem_stage_src1_vector(const ggml_tensor * src1, float * x, uint32_t k) {
    if (src1->nb[0] == (int64_t) sizeof(float)) {
        std::memcpy(x, src1->data, k * sizeof(float));
        return;
    }
    for (uint32_t col = 0; col < k; ++col) {
        const char * src = (const char *) src1->data + col * src1->nb[0];
        std::memcpy(x + col, src, sizeof(float));
    }
}

static bool ggml_downmem_validate_result(const ggml_tensor * src0, const ggml_tensor * src1, const ggml_tensor * dst, const float * y) {
    const char * enabled = std::getenv("GGML_DOWNMEM_VALIDATE");
    if (enabled == nullptr || std::strcmp(enabled, "0") == 0) {
        return true;
    }

    const uint32_t k = (uint32_t) src0->ne[0];
    const uint32_t n = (uint32_t) src0->ne[1];
    std::vector<float> x(k, 0.0f);
    ggml_downmem_stage_src1_vector(src1, x.data(), k);

    for (uint32_t row = 0; row < n; ++row) {
        float expected = 0.0f;
        const float * a_row = (const float *) ((const char *) src0->data + row * src0->nb[1]);
        for (uint32_t col = 0; col < k; ++col) {
            expected += a_row[col] * x[col];
        }

        const float got = y[row];
        const float diff = std::fabs(got - expected);
        const float scale = std::max(1.0f, std::fabs(expected));
        if (!(diff <= 1.0e-4f || diff <= scale * 1.0e-4f)) {
            GGML_LOG_ERROR("%s: validation failed row=%u expected=%g got=%g diff=%g\n",
                           __func__, row, (double) expected, (double) got, (double) diff);
            GGML_UNUSED(dst);
            return false;
        }
    }

    return true;
}
```

- [ ] **Step 3: Add GEMV execution function**

Add after the helpers:

```cpp
static bool ggml_downmem_mul_mat_f32_gemv(ggml_backend_downmem_context * ctx, ggml_tensor * dst) {
    const ggml_tensor * src0 = dst->src[0];
    const ggml_tensor * src1 = dst->src[1];

    if (!ggml_downmem_supports_mul_mat_f32_gemv(dst, true)) {
        GGML_LOG_ERROR("%s: scheduled unsupported op\n", __func__);
        return false;
    }
    if (!ggml_downmem_load(ctx)) {
        return false;
    }

    const uint32_t k = (uint32_t) src0->ne[0];
    const uint32_t n = (uint32_t) src0->ne[1];
    const uint32_t k_pad = ggml_downmem_align_up_u32(k, 2);

    std::vector<dpu_rows_t> layout(ctx->nr_dpus);
    uint32_t max_rows = 0;
    for (uint32_t d = 0; d < ctx->nr_dpus; ++d) {
        const uint32_t base = n / ctx->nr_dpus;
        const uint32_t rest = n % ctx->nr_dpus;
        layout[d].rows = base + (d < rest ? 1u : 0u);
        layout[d].row_offset = d * base + (d < rest ? d : rest);
        max_rows = std::max(max_rows, layout[d].rows);
    }
    max_rows = std::max(max_rows, 1u);

    const uint32_t a_floats_per_dpu = max_rows * k_pad;
    const uint32_t x_floats_per_dpu = k_pad;
    const uint32_t y_floats_per_dpu = max_rows;
    const uint32_t a_bytes = a_floats_per_dpu * sizeof(float);
    const uint32_t x_bytes = x_floats_per_dpu * sizeof(float);
    const uint32_t y_bytes = y_floats_per_dpu * sizeof(float);

    const uint64_t total_mram_bytes = (uint64_t) a_bytes + x_bytes + y_bytes;
    if (total_mram_bytes > 64ull * 1024ull * 1024ull) {
        GGML_LOG_ERROR("%s: MRAM layout too large: %" PRIu64 " bytes\n", __func__, total_mram_bytes);
        return false;
    }

    std::vector<float> a_padded((size_t) ctx->nr_dpus * a_floats_per_dpu, 0.0f);
    std::vector<float> x(k_pad, 0.0f);
    std::vector<float> y_padded((size_t) ctx->nr_dpus * max_rows, 0.0f);
    std::vector<float> y(n, 0.0f);
    std::vector<llama_gemv_f32_args_t> args(ctx->nr_dpus);

    ggml_downmem_stage_src1_vector(src1, x.data(), k);

    std::vector<float> a_rows((size_t) n * k_pad, 0.0f);
    ggml_downmem_stage_src0_row_major(src0, a_rows.data(), k, n, k_pad);

    for (uint32_t d = 0; d < ctx->nr_dpus; ++d) {
        for (uint32_t row = 0; row < layout[d].rows; ++row) {
            const uint32_t global_row = layout[d].row_offset + row;
            std::memcpy(a_padded.data() + (size_t) d * a_floats_per_dpu + row * k_pad,
                        a_rows.data() + (size_t) global_row * k_pad,
                        k_pad * sizeof(float));
        }
        args[d] = llama_gemv_f32_args_t {
            /* .k        = */ k,
            /* .k_pad    = */ k_pad,
            /* .n_rows   = */ layout[d].rows,
            /* .max_rows = */ max_rows,
            /* .a_offset = */ 0,
            /* .x_offset = */ a_bytes,
            /* .y_offset = */ a_bytes + x_bytes,
        };
    }

    dpu_set_t each;
    uint32_t idx = 0;
    DPU_FOREACH(ctx->set, each, idx) {
        if (dpu_prepare_xfer(each, args.data() + idx) != DPU_OK) {
            return false;
        }
    }
    if (dpu_push_xfer(ctx->set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0,
                      sizeof(llama_gemv_f32_args_t), DPU_XFER_DEFAULT) != DPU_OK) {
        return false;
    }

    idx = 0;
    DPU_FOREACH(ctx->set, each, idx) {
        if (dpu_prepare_xfer(each, a_padded.data() + (size_t) idx * a_floats_per_dpu) != DPU_OK) {
            return false;
        }
    }
    if (dpu_push_xfer(ctx->set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                      a_bytes, DPU_XFER_DEFAULT) != DPU_OK) {
        return false;
    }

    if (dpu_broadcast_to(ctx->set, DPU_MRAM_HEAP_POINTER_NAME, a_bytes,
                         x.data(), x_bytes, DPU_XFER_DEFAULT) != DPU_OK) {
        return false;
    }

    if (dpu_launch(ctx->set, DPU_SYNCHRONOUS) != DPU_OK) {
        return false;
    }

    idx = 0;
    DPU_FOREACH(ctx->set, each, idx) {
        if (dpu_prepare_xfer(each, y_padded.data() + (size_t) idx * max_rows) != DPU_OK) {
            return false;
        }
    }
    if (dpu_push_xfer(ctx->set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, a_bytes + x_bytes,
                      y_bytes, DPU_XFER_DEFAULT) != DPU_OK) {
        return false;
    }

    for (uint32_t d = 0; d < ctx->nr_dpus; ++d) {
        for (uint32_t row = 0; row < layout[d].rows; ++row) {
            y[layout[d].row_offset + row] = y_padded[(size_t) d * max_rows + row];
        }
    }

    if (!ggml_downmem_validate_result(src0, src1, dst, y.data())) {
        return false;
    }

    for (uint32_t row = 0; row < n; ++row) {
        float * dst_elem = (float *) ((char *) dst->data + row * dst->nb[0]);
        *dst_elem = y[row];
    }

    ++ctx->executed_ops;
    if (ggml_downmem_verbose()) {
        GGML_LOG_INFO("%s: executed op=%d k=%u n=%u dpus=%u max_rows=%u bytes=%" PRIu64 "\n",
                      __func__, ctx->executed_ops, k, n, ctx->nr_dpus, max_rows, total_mram_bytes);
    }

    return true;
}
```

- [ ] **Step 4: Update backend free**

Replace `ggml_backend_downmem_free` with:

```cpp
static void ggml_backend_downmem_free(ggml_backend_t backend) {
    auto * ctx = (ggml_backend_downmem_context *) backend->context;
    if (ctx->loaded) {
        dpu_free(ctx->set);
    }
    delete ctx;
    delete backend;
}
```

- [ ] **Step 5: Update graph_compute**

Replace `ggml_backend_downmem_graph_compute` with:

```cpp
static enum ggml_status ggml_backend_downmem_graph_compute(ggml_backend_t backend, struct ggml_cgraph * cgraph) {
    auto * ctx = (ggml_backend_downmem_context *) backend->context;

    for (int i = 0; i < cgraph->n_nodes; ++i) {
        ggml_tensor * node = cgraph->nodes[i];

        if ((node->flags & GGML_TENSOR_FLAG_COMPUTE) == 0) {
            continue;
        }

        switch (node->op) {
            case GGML_OP_MUL_MAT:
                if (!ggml_downmem_mul_mat_f32_gemv(ctx, node)) {
                    return GGML_STATUS_FAILED;
                }
                break;

            case GGML_OP_NONE:
            case GGML_OP_RESHAPE:
            case GGML_OP_VIEW:
            case GGML_OP_PERMUTE:
            case GGML_OP_TRANSPOSE:
                break;

            default:
                GGML_LOG_ERROR("%s: unsupported scheduled op %s\n", __func__, ggml_op_desc(node));
                return GGML_STATUS_FAILED;
        }
    }

    return GGML_STATUS_SUCCESS;
}
```

- [ ] **Step 6: Build llama.cpp**

Run:

```bash
cd /home/fjg/src/llama.cpp
cmake --build build-downmem --target ggml llama-cli -j"$(nproc)"
```

Expected:

```text
Built target ggml-downmem
Built target ggml
Built target llama-cli
```

- [ ] **Step 7: Commit host bridge**

Run:

```bash
cd /home/fjg/src/llama.cpp
git add ggml/src/ggml-downmem/ggml-downmem.cpp
git commit -m "Implement downmem F32 GEMV bridge"
```

Expected:

```text
[... Implement downmem F32 GEMV bridge]
```

---

## Task 9: Add Controlled Host Dequantization for Quantized Weights

**Files:**
- Modify: `/home/fjg/src/llama.cpp/ggml/src/ggml-downmem/ggml-downmem.cpp`

- [ ] **Step 1: Add quantization enable helper**

Add after `ggml_downmem_verbose()`:

```cpp
static bool ggml_downmem_allow_quant_dequant(void) {
    const char * value = std::getenv("GGML_DOWNMEM_ALLOW_QUANT_DEQUANT");
    return value != nullptr && std::strcmp(value, "0") != 0;
}
```

- [ ] **Step 2: Replace tensor type helper with src0/src1-specific helpers**

Replace `ggml_downmem_tensor_is_plain_f32_1d_or_2d` with:

```cpp
static bool ggml_downmem_src1_is_plain_f32_vector(const ggml_tensor * t) {
    return t != nullptr &&
           t->type == GGML_TYPE_F32 &&
           t->nb[0] == (int64_t) sizeof(float) &&
           t->nb[1] >= t->nb[0] * t->ne[0];
}

static bool ggml_downmem_src0_can_stage_to_f32(const ggml_tensor * t) {
    if (t == nullptr) {
        return false;
    }
    if (t->type == GGML_TYPE_F32) {
        return t->nb[0] == (int64_t) sizeof(float) &&
               t->nb[1] >= t->nb[0] * t->ne[0];
    }
    if (!ggml_downmem_allow_quant_dequant()) {
        return false;
    }
    const ggml_type_traits * traits = ggml_get_type_traits(t->type);
    return traits != nullptr && traits->to_float != nullptr;
}
```

- [ ] **Step 3: Update supported-op predicate**

Replace this block:

```cpp
    if (!ggml_downmem_tensor_is_plain_f32_1d_or_2d(src0) ||
        !ggml_downmem_tensor_is_plain_f32_1d_or_2d(src1)) {
        return false;
    }
```

with:

```cpp
    if (!ggml_downmem_src0_can_stage_to_f32(src0) ||
        !ggml_downmem_src1_is_plain_f32_vector(src1)) {
        return false;
    }
```

- [ ] **Step 4: Replace src0 staging helper**

Replace `ggml_downmem_stage_src0_row_major` with:

```cpp
static bool ggml_downmem_stage_src0_row_major(const ggml_tensor * src0, float * a_rows, uint32_t k, uint32_t n, uint32_t k_pad) {
    if (src0->type == GGML_TYPE_F32) {
        for (uint32_t row = 0; row < n; ++row) {
            const char * src_row = (const char *) src0->data + row * src0->nb[1];
            std::memcpy(a_rows + (size_t) row * k_pad, src_row, k * sizeof(float));
        }
        return true;
    }

    if (!ggml_downmem_allow_quant_dequant()) {
        return false;
    }

    const ggml_type_traits * traits = ggml_get_type_traits(src0->type);
    if (traits == nullptr || traits->to_float == nullptr) {
        return false;
    }

    for (uint32_t row = 0; row < n; ++row) {
        const char * src_row = (const char *) src0->data + row * src0->nb[1];
        traits->to_float(src_row, a_rows + (size_t) row * k_pad, k);
    }
    return true;
}
```

- [ ] **Step 5: Replace validation helper to use staged A rows**

Replace `ggml_downmem_validate_result` with:

```cpp
static bool ggml_downmem_validate_result(const ggml_tensor * src1, const float * a_rows, const float * y, uint32_t k, uint32_t n, uint32_t k_pad) {
    const char * enabled = std::getenv("GGML_DOWNMEM_VALIDATE");
    if (enabled == nullptr || std::strcmp(enabled, "0") == 0) {
        return true;
    }

    std::vector<float> x(k, 0.0f);
    ggml_downmem_stage_src1_vector(src1, x.data(), k);

    for (uint32_t row = 0; row < n; ++row) {
        float expected = 0.0f;
        const float * a_row = a_rows + (size_t) row * k_pad;
        for (uint32_t col = 0; col < k; ++col) {
            expected += a_row[col] * x[col];
        }

        const float got = y[row];
        const float diff = std::fabs(got - expected);
        const float scale = std::max(1.0f, std::fabs(expected));
        if (!(diff <= 1.0e-4f || diff <= scale * 1.0e-4f)) {
            GGML_LOG_ERROR("%s: validation failed row=%u expected=%g got=%g diff=%g\n",
                           __func__, row, (double) expected, (double) got, (double) diff);
            return false;
        }
    }

    return true;
}
```

- [ ] **Step 6: Update GEMV execution to handle staging failure and new validation signature**

Replace:

```cpp
    ggml_downmem_stage_src0_row_major(src0, a_rows.data(), k, n, k_pad);
```

with:

```cpp
    if (!ggml_downmem_stage_src0_row_major(src0, a_rows.data(), k, n, k_pad)) {
        GGML_LOG_ERROR("%s: failed to stage src0 type %s\n", __func__, ggml_type_name(src0->type));
        return false;
    }
```

Replace:

```cpp
    if (!ggml_downmem_validate_result(src0, src1, dst, y.data())) {
        return false;
    }
```

with:

```cpp
    if (!ggml_downmem_validate_result(src1, a_rows.data(), y.data(), k, n, k_pad)) {
        return false;
    }
```

- [ ] **Step 7: Build**

Run:

```bash
cd /home/fjg/src/llama.cpp
cmake --build build-downmem --target ggml llama-cli -j"$(nproc)"
```

Expected:

```text
Built target ggml-downmem
Built target ggml
Built target llama-cli
```

- [ ] **Step 8: Verify quantized model can claim an op only when dequant is enabled**

Run:

```bash
cd /home/fjg/src/llama.cpp
DMM_NR_SIM_THRDS=4 \
GGML_DOWNMEM=1 \
GGML_DOWNMEM_DPU_BIN=/home/fjg/src/downmem/build-rv/devApp/rvbins/LLAMA_GEMV_F32 \
GGML_DOWNMEM_NR_DPUS=4 \
GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1 \
GGML_DOWNMEM_MAX_OPS=1 \
GGML_DOWNMEM_VERBOSE=1 \
./build-downmem/bin/llama-cli \
  -m /home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf \
  -p "Once upon a time" \
  -n 1 \
  -t 1 \
  --temp 0 \
  --no-display-prompt
```

Expected:

```text
claiming MUL_MAT
```

and command exits `0`.

- [ ] **Step 9: Commit quant staging**

Run:

```bash
cd /home/fjg/src/llama.cpp
git add ggml/src/ggml-downmem/ggml-downmem.cpp
git commit -m "Allow controlled host dequantization for downmem offload"
```

Expected:

```text
[... Allow controlled host dequantization for downmem offload]
```

---

## Task 10: Add ggml-Level Downmem Backend Test

**Files:**
- Create: `/home/fjg/src/llama.cpp/tests/test-downmem-backend.cpp`
- Modify: `/home/fjg/src/llama.cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Add test source**

Create `/home/fjg/src/llama.cpp/tests/test-downmem-backend.cpp` with:

```cpp
#include "ggml.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static float pattern_a(int row, int col) {
    const int v = (row * 17 + col * 13 + 7) % 23 - 11;
    return (float) v * 0.03125f;
}

static float pattern_x(int col) {
    const int v = (col * 19 + 5) % 29 - 14;
    return (float) v * 0.0625f;
}

static void fill_tensor_2d_f32(ggml_tensor * t, int rows, int cols) {
    for (int row = 0; row < rows; ++row) {
        float * dst = (float *) ((char *) t->data + row * t->nb[1]);
        for (int col = 0; col < cols; ++col) {
            dst[col] = pattern_a(row, col);
        }
    }
}

static void fill_vector_f32(ggml_tensor * t, int cols) {
    float * dst = (float *) t->data;
    for (int col = 0; col < cols; ++col) {
        dst[col] = pattern_x(col);
    }
}

static std::vector<float> run_graph(ggml_backend_t backend, int n, int k) {
    const size_t mem_size = 16u * 1024u * 1024u;
    std::vector<uint8_t> mem(mem_size);
    ggml_init_params params = {
        /* .mem_size   = */ mem_size,
        /* .mem_buffer = */ mem.data(),
        /* .no_alloc   = */ false,
    };

    ggml_context * ctx = ggml_init(params);
    ggml_tensor * a = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, k, n);
    ggml_tensor * x = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, k, 1);
    fill_tensor_2d_f32(a, n, k);
    fill_vector_f32(x, k);

    ggml_tensor * y = ggml_mul_mat(ctx, a, x);
    ggml_cgraph * graph = ggml_new_graph(ctx);
    ggml_build_forward_expand(graph, y);

    const ggml_status status = ggml_backend_graph_compute(backend, graph);
    if (status != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "graph compute failed: %d\n", (int) status);
        std::exit(1);
    }

    std::vector<float> result(n);
    for (int row = 0; row < n; ++row) {
        result[row] = *(float *) ((char *) y->data + row * y->nb[0]);
    }

    ggml_free(ctx);
    return result;
}

int main(void) {
    const char * enabled = std::getenv("GGML_DOWNMEM");
    const char * bin = std::getenv("GGML_DOWNMEM_DPU_BIN");
    if (enabled == nullptr || std::strcmp(enabled, "0") == 0 || bin == nullptr || bin[0] == '\0') {
        std::puts("SKIP test-downmem-backend: GGML_DOWNMEM and GGML_DOWNMEM_DPU_BIN are required");
        return 77;
    }

    const int n = 17;
    const int k = 31;

    ggml_backend_t cpu = ggml_backend_cpu_init();
    if (cpu == nullptr) {
        fprintf(stderr, "failed to init CPU backend\n");
        return 1;
    }
    std::vector<float> expected = run_graph(cpu, n, k);
    ggml_backend_free(cpu);

    ggml_backend_dev_t dev = nullptr;
    const size_t n_devs = ggml_backend_dev_count();
    for (size_t i = 0; i < n_devs; ++i) {
        ggml_backend_dev_t candidate = ggml_backend_dev_get(i);
        if (std::strcmp(ggml_backend_dev_name(candidate), "Downmem") == 0) {
            dev = candidate;
            break;
        }
    }
    if (dev == nullptr) {
        fprintf(stderr, "Downmem backend device not registered\n");
        return 1;
    }

    ggml_backend_t downmem = ggml_backend_dev_init(dev, nullptr);
    if (downmem == nullptr) {
        fprintf(stderr, "failed to init Downmem backend\n");
        return 1;
    }
    std::vector<float> got = run_graph(downmem, n, k);
    ggml_backend_free(downmem);

    for (int i = 0; i < n; ++i) {
        const float diff = std::fabs(got[i] - expected[i]);
        const float scale = std::fmax(1.0f, std::fabs(expected[i]));
        if (!(diff <= 1.0e-4f || diff <= scale * 1.0e-4f)) {
            fprintf(stderr, "row=%d expected=%g got=%g diff=%g\n",
                    i, (double) expected[i], (double) got[i], (double) diff);
            return 1;
        }
    }

    std::puts("test-downmem-backend passed");
    return 0;
}
```

- [ ] **Step 2: Add test to CMake**

Modify `/home/fjg/src/llama.cpp/tests/CMakeLists.txt` after `llama_build_and_test(test-backend-ops.cpp)`:

```cmake
if (GGML_DOWNMEM)
    llama_build(test-downmem-backend.cpp)
    llama_test(test-downmem-backend LABEL "backend")
endif()
```

- [ ] **Step 3: Build test**

Run:

```bash
cd /home/fjg/src/llama.cpp
cmake --build build-downmem --target test-downmem-backend -j"$(nproc)"
```

Expected:

```text
Built target test-downmem-backend
```

- [ ] **Step 4: Run test manually with validation**

Run:

```bash
cd /home/fjg/src/llama.cpp
DMM_NR_SIM_THRDS=4 \
GGML_DOWNMEM=1 \
GGML_DOWNMEM_DPU_BIN=/home/fjg/src/downmem/build-rv/devApp/rvbins/LLAMA_GEMV_F32 \
GGML_DOWNMEM_NR_DPUS=4 \
GGML_DOWNMEM_VALIDATE=1 \
GGML_DOWNMEM_VERBOSE=1 \
./build-downmem/bin/test-downmem-backend
```

Expected includes:

```text
test-downmem-backend passed
```

and at least one log like:

```text
executed op=1 k=31 n=17
```

- [ ] **Step 5: Run CTest version**

Run:

```bash
cd /home/fjg/src/llama.cpp
DMM_NR_SIM_THRDS=4 \
GGML_DOWNMEM=1 \
GGML_DOWNMEM_DPU_BIN=/home/fjg/src/downmem/build-rv/devApp/rvbins/LLAMA_GEMV_F32 \
GGML_DOWNMEM_NR_DPUS=4 \
GGML_DOWNMEM_VALIDATE=1 \
ctest --test-dir build-downmem -R test-downmem-backend --output-on-failure
```

Expected:

```text
100% tests passed
```

- [ ] **Step 6: Commit ggml test**

Run:

```bash
cd /home/fjg/src/llama.cpp
git add tests/test-downmem-backend.cpp tests/CMakeLists.txt
git commit -m "Test downmem ggml F32 GEMV backend"
```

Expected:

```text
[... Test downmem ggml F32 GEMV backend]
```

---

## Task 11: End-to-End llama.cpp Smoke Script

**Files:**
- Create: `/home/fjg/src/llama.cpp/scripts/downmem-smoke.sh`

- [ ] **Step 1: Create smoke script**

Create `/home/fjg/src/llama.cpp/scripts/downmem-smoke.sh` with:

```bash
#!/usr/bin/env bash
set -euo pipefail

LLAMA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOWNMEM_ROOT="${DOWNMEM_ROOT:-/home/fjg/src/downmem}"
DOWNMEM_BUILD="${DOWNMEM_BUILD:-${DOWNMEM_ROOT}/build-rv}"
LLAMA_BUILD="${LLAMA_BUILD:-${LLAMA_ROOT}/build-downmem}"
DPU_BIN="${GGML_DOWNMEM_DPU_BIN:-${DOWNMEM_BUILD}/devApp/rvbins/LLAMA_GEMV_F32}"
MODEL="${MODEL:-${LLAMA_ROOT}/models/stories15M-q4_0.gguf}"

cmake --build "${DOWNMEM_BUILD}" --target rvLLAMA_GEMV_F32 dmmLLAMA_GEMV_F32 -j"$(nproc)"

cmake -S "${LLAMA_ROOT}" -B "${LLAMA_BUILD}" \
  -DGGML_DOWNMEM=ON \
  -DGGML_DOWNMEM_ROOT="${DOWNMEM_ROOT}" \
  -DGGML_DOWNMEM_LIB="${DOWNMEM_BUILD}/libdmm.a" \
  -DLLAMA_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "${LLAMA_BUILD}" --target test-downmem-backend llama-cli -j"$(nproc)"

DMM_NR_SIM_THRDS="${DMM_NR_SIM_THRDS:-4}" \
GGML_DOWNMEM=1 \
GGML_DOWNMEM_DPU_BIN="${DPU_BIN}" \
GGML_DOWNMEM_NR_DPUS="${GGML_DOWNMEM_NR_DPUS:-4}" \
GGML_DOWNMEM_VALIDATE=1 \
GGML_DOWNMEM_VERBOSE=1 \
"${LLAMA_BUILD}/bin/test-downmem-backend"

DMM_NR_SIM_THRDS="${DMM_NR_SIM_THRDS:-4}" \
GGML_DOWNMEM=1 \
GGML_DOWNMEM_DPU_BIN="${DPU_BIN}" \
GGML_DOWNMEM_NR_DPUS="${GGML_DOWNMEM_NR_DPUS:-4}" \
GGML_DOWNMEM_MAX_OPS="${GGML_DOWNMEM_MAX_OPS:-1}" \
GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1 \
GGML_DOWNMEM_VERBOSE=1 \
"${LLAMA_BUILD}/bin/llama-cli" \
  -m "${MODEL}" \
  -p "Once upon a time" \
  -n 8 \
  -t 1 \
  --temp 0 \
  --no-display-prompt
```

- [ ] **Step 2: Make script executable**

Run:

```bash
cd /home/fjg/src/llama.cpp
chmod +x scripts/downmem-smoke.sh
```

- [ ] **Step 3: Run script**

Run:

```bash
cd /home/fjg/src/llama.cpp
scripts/downmem-smoke.sh
```

Expected includes:

```text
test-downmem-backend passed
```

Expected llama output includes at least one downmem verbose line:

```text
claiming MUL_MAT
```

or:

```text
executed op=1
```

The script exits `0`.

- [ ] **Step 4: Commit smoke script**

Run:

```bash
cd /home/fjg/src/llama.cpp
git add scripts/downmem-smoke.sh
git commit -m "Add downmem llama smoke script"
```

Expected:

```text
[... Add downmem llama smoke script]
```

---

## Task 12: End-to-End Token Stability Check

**Files:**
- No code changes unless a failure is found.

- [ ] **Step 1: Capture CPU baseline**

Run:

```bash
cd /home/fjg/src/llama.cpp
./build-downmem/bin/llama-cli \
  -m /home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf \
  -p "Once upon a time" \
  -n 16 \
  -t 1 \
  --temp 0 \
  --no-display-prompt \
  > /tmp/llama-cpu.out
```

Expected: command exits `0`.

- [ ] **Step 2: Capture downmem-enabled output with one offloaded op**

Run:

```bash
cd /home/fjg/src/llama.cpp
DMM_NR_SIM_THRDS=4 \
GGML_DOWNMEM=1 \
GGML_DOWNMEM_DPU_BIN=/home/fjg/src/downmem/build-rv/devApp/rvbins/LLAMA_GEMV_F32 \
GGML_DOWNMEM_NR_DPUS=4 \
GGML_DOWNMEM_MAX_OPS=1 \
GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1 \
GGML_DOWNMEM_VERBOSE=1 \
./build-downmem/bin/llama-cli \
  -m /home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf \
  -p "Once upon a time" \
  -n 16 \
  -t 1 \
  --temp 0 \
  --no-display-prompt \
  > /tmp/llama-downmem.out 2> /tmp/llama-downmem.err
```

Expected: command exits `0`.

- [ ] **Step 3: Verify offload happened**

Run:

```bash
grep -E "claiming MUL_MAT|executed op=1" /tmp/llama-downmem.err /tmp/llama-downmem.out
```

Expected includes one matching line.

- [ ] **Step 4: Compare generated text**

Run:

```bash
diff -u /tmp/llama-cpu.out /tmp/llama-downmem.out
```

Expected:

```text
```

Exit status `0`.

- [ ] **Step 5: If output differs, rerun with numeric validation**

Run only if Step 4 fails:

```bash
cd /home/fjg/src/llama.cpp
DMM_NR_SIM_THRDS=4 \
GGML_DOWNMEM=1 \
GGML_DOWNMEM_DPU_BIN=/home/fjg/src/downmem/build-rv/devApp/rvbins/LLAMA_GEMV_F32 \
GGML_DOWNMEM_NR_DPUS=4 \
GGML_DOWNMEM_MAX_OPS=1 \
GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1 \
GGML_DOWNMEM_VALIDATE=1 \
GGML_DOWNMEM_VERBOSE=1 \
./build-downmem/bin/llama-cli \
  -m /home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf \
  -p "Once upon a time" \
  -n 16 \
  -t 1 \
  --temp 0 \
  --no-display-prompt
```

Expected: either exits `0` or prints a validation failure pinpointing the row.

- [ ] **Step 6: Commit only if code changes were needed**

If no code changes were made, do not commit.

If fixes were made:

```bash
cd /home/fjg/src/llama.cpp
git add ggml/src/ggml-downmem/ggml-downmem.cpp tests/test-downmem-backend.cpp scripts/downmem-smoke.sh
git commit -m "Fix downmem llama smoke validation"
```

Expected:

```text
[... Fix downmem llama smoke validation]
```

---

## Task 13: Negative Tests and Final Verification

**Files:**
- Read-only unless failures require fixes.

- [ ] **Step 1: Verify downmem standalone test**

Run:

```bash
cd /home/fjg/src/downmem
scripts/run-llama-gemv-f32-test.sh
```

Expected:

```text
LLAMA_GEMV_F32 passed
```

- [ ] **Step 2: Verify ggml backend test**

Run:

```bash
cd /home/fjg/src/llama.cpp
DMM_NR_SIM_THRDS=4 \
GGML_DOWNMEM=1 \
GGML_DOWNMEM_DPU_BIN=/home/fjg/src/downmem/build-rv/devApp/rvbins/LLAMA_GEMV_F32 \
GGML_DOWNMEM_NR_DPUS=4 \
GGML_DOWNMEM_VALIDATE=1 \
ctest --test-dir build-downmem -R test-downmem-backend --output-on-failure
```

Expected:

```text
100% tests passed
```

- [ ] **Step 3: Verify disabled env does not offload**

Run:

```bash
cd /home/fjg/src/llama.cpp
GGML_DOWNMEM_VERBOSE=1 \
./build-downmem/bin/test-downmem-backend
```

Expected:

```text
SKIP test-downmem-backend: GGML_DOWNMEM and GGML_DOWNMEM_DPU_BIN are required
```

Exit status is `77`.

- [ ] **Step 4: Verify invalid DPU binary does not crash llama**

Run:

```bash
cd /home/fjg/src/llama.cpp
GGML_DOWNMEM=1 \
GGML_DOWNMEM_DPU_BIN=/does/not/exist \
GGML_DOWNMEM_VERBOSE=1 \
./build-downmem/bin/llama-cli \
  -m /home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf \
  -p "Once upon a time" \
  -n 1 \
  -t 1 \
  --temp 0 \
  --no-display-prompt
```

Expected: exits `0`; no DPU transfer/launch logs.

- [ ] **Step 5: Verify end-to-end smoke**

Run:

```bash
cd /home/fjg/src/llama.cpp
scripts/downmem-smoke.sh
```

Expected:

```text
test-downmem-backend passed
```

and llama run exits `0` with downmem offload logs.

- [ ] **Step 6: Check git status in both repos**

Run:

```bash
cd /home/fjg/src/downmem
git status --short
cd /home/fjg/src/llama.cpp
git status --short
```

Expected downmem still shows user pre-existing changes plus no accidental untracked build outputs:

```text
 M .gitignore
?? setup_downmem_upmem_env.sh
```

Expected llama.cpp:

```text
```

If implementation commits are complete, status should be clean in llama.cpp.

---

## Risk Register

- `llama.cpp` scheduler may not route real `stories15M-q4_0.gguf` ops to downmem unless `GGML_DOWNMEM_ALLOW_QUANT_DEQUANT=1` is set. The DPU kernel remains F32-only; quantized weights are dequantized on the host before transfer.

- `offload_op` currently uses a static counter. This is acceptable for the MVP but process-global. If repeated tests in one process need deterministic reclaims, move the counter into device or backend context in a follow-up.

- The first bridge stages all rows into host vectors per op. This is intentionally simple and not optimized. Do not optimize transfer reuse until correctness is proven.

- The DPU kernel uses soft-float. Correctness is the first milestone; performance will likely be poor compared with CPU for small shapes.

- CMake linkage to `libdmm.a` may expose missing transitive libraries on this machine. If build fails at link time, add only the missing library reported by the linker to `ggml/src/ggml-downmem/CMakeLists.txt` and rerun the exact build command.

---

## Completion Criteria

- `/home/fjg/src/downmem/scripts/run-llama-gemv-f32-test.sh` exits `0` and prints `LLAMA_GEMV_F32 passed`.
- `/home/fjg/src/llama.cpp/build-downmem/bin/test-downmem-backend` exits `0` with `GGML_DOWNMEM=1` and validation enabled.
- `scripts/downmem-smoke.sh` runs a real `llama-cli` prompt with `/home/fjg/src/llama.cpp/models/stories15M-q4_0.gguf`.
- Logs show at least one claimed or executed downmem `MUL_MAT` in the enabled run.
- CPU-only run still works with `GGML_DOWNMEM` unset.
- No user-owned changes are reverted in `/home/fjg/src/downmem`.
