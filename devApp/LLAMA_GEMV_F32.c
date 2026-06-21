#include "moredefs.h"
#include <alloc.h>
#include <stdint.h>

#define LLAMA_DOWNMEM_OP_MUL_MAT_F32 0u
#define LLAMA_DOWNMEM_OP_SCALE_F32 1u

typedef struct {
  uint32_t opcode;
  uint32_t k;
  uint32_t k_pad;
  uint32_t n_rows;
  uint32_t max_rows;
  uint32_t m;
  uint32_t n_elems;
  uint32_t max_elems;
  uint32_t a_offset;
  uint32_t x_offset;
  uint32_t y_offset;
  float scale;
  float bias;
} llama_gemv_f32_args_t;

__host llama_gemv_f32_args_t DPU_INPUT_ARGUMENTS;

ALL_THREADS_BARRIER_INIT();

#define LLAMA_GEMV_CHUNK_FLOATS 64u

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

static uint32_t elems_start_for_tasklet(uint32_t tasklet_id, uint32_t n_elems) {
  const uint32_t base = n_elems / NR_TASKLETS;
  const uint32_t rest = n_elems % NR_TASKLETS;
  return tasklet_id * base + (tasklet_id < rest ? tasklet_id : rest);
}

static uint32_t elems_count_for_tasklet(uint32_t tasklet_id, uint32_t n_elems) {
  const uint32_t base = n_elems / NR_TASKLETS;
  const uint32_t rest = n_elems % NR_TASKLETS;
  return base + (tasklet_id < rest ? 1u : 0u);
}

int main(void) {
  const uint32_t tasklet_id = me();
  const uint32_t k = DPU_INPUT_ARGUMENTS.k;
  const uint32_t k_pad = DPU_INPUT_ARGUMENTS.k_pad;
  const uint32_t n_rows = DPU_INPUT_ARGUMENTS.n_rows;
  const uint32_t m = DPU_INPUT_ARGUMENTS.m;
  const uint32_t a_offset = DPU_INPUT_ARGUMENTS.a_offset;
  const uint32_t x_offset = DPU_INPUT_ARGUMENTS.x_offset;
  const uint32_t y_offset = DPU_INPUT_ARGUMENTS.y_offset;

  float a_buf[LLAMA_GEMV_CHUNK_FLOATS];
  float x_buf[LLAMA_GEMV_CHUNK_FLOATS];
  float y_buf[1];

  all_threads_barrier_wait();

  if (DPU_INPUT_ARGUMENTS.opcode == LLAMA_DOWNMEM_OP_SCALE_F32) {
    const uint32_t n_elems = DPU_INPUT_ARGUMENTS.n_elems;
    const float scale = DPU_INPUT_ARGUMENTS.scale;
    const float bias = DPU_INPUT_ARGUMENTS.bias;
    const uint32_t elem_start = elems_start_for_tasklet(tasklet_id, n_elems);
    const uint32_t elem_count = elems_count_for_tasklet(tasklet_id, n_elems);
    const uint32_t elem_end = elem_start + elem_count;

    for (uint32_t elem = elem_start; elem < elem_end; elem += LLAMA_GEMV_CHUNK_FLOATS) {
      uint32_t chunk = elem_end - elem;
      if (chunk > LLAMA_GEMV_CHUNK_FLOATS) {
        chunk = LLAMA_GEMV_CHUNK_FLOATS;
      }

      const uint32_t bytes = chunk * sizeof(float);
      const uintptr_t x_addr = (uintptr_t)DPU_MRAM_HEAP_POINTER + x_offset + elem * sizeof(float);
      const uintptr_t y_addr = (uintptr_t)DPU_MRAM_HEAP_POINTER + y_offset + elem * sizeof(float);

      mram_read((__mram_ptr void const *)x_addr, x_buf, bytes);
      for (uint32_t i = 0; i < chunk; ++i) {
        x_buf[i] = x_buf[i] * scale + bias;
      }
      mram_write(x_buf, (__mram_ptr void *)y_addr, bytes);
    }

    return 0;
  }

  const uint32_t row_start = rows_start_for_tasklet(tasklet_id, n_rows);
  const uint32_t row_count = rows_count_for_tasklet(tasklet_id, n_rows);
  const uint32_t row_end = row_start + row_count;

  for (uint32_t row = row_start; row < row_end; ++row) {
    for (uint32_t out_col = 0; out_col < m; ++out_col) {
      float sum = 0.0f;

      for (uint32_t col = 0; col < k; col += LLAMA_GEMV_CHUNK_FLOATS) {
        uint32_t chunk = k - col;
        if (chunk > LLAMA_GEMV_CHUNK_FLOATS) {
          chunk = LLAMA_GEMV_CHUNK_FLOATS;
        }

        const uint32_t bytes = chunk * sizeof(float);
        const uintptr_t a_addr = (uintptr_t)DPU_MRAM_HEAP_POINTER + a_offset +
                                 (row * k_pad + col) * sizeof(float);
        const uintptr_t x_addr = (uintptr_t)DPU_MRAM_HEAP_POINTER + x_offset +
                                 (out_col * k_pad + col) * sizeof(float);

        mram_read((__mram_ptr void const *)a_addr, a_buf, bytes);
        mram_read((__mram_ptr void const *)x_addr, x_buf, bytes);

        for (uint32_t i = 0; i < chunk; ++i) {
          sum += a_buf[i] * x_buf[i];
        }
      }

      y_buf[0] = sum;
      const uintptr_t y_addr = (uintptr_t)DPU_MRAM_HEAP_POINTER + y_offset +
                               (row * m + out_col) * sizeof(float);
      mram_write(y_buf, (__mram_ptr void *)y_addr, sizeof(float));
    }
  }

  return 0;
}
