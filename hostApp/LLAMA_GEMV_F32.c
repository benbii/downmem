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

static void host_gemv(const float *a, const float *x, float *y, uint32_t n,
                      uint32_t k) {
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

static int run_case(uint32_t n, uint32_t k, uint32_t nr_dpus,
                    const char *dpu_binary) {
  struct dpu_set_t set;

  DMM_VERIFY(dpu_alloc(nr_dpus, NULL, &set));
  DMM_VERIFY(dpu_load(set, dpu_binary, NULL));

  uint32_t actual_dpus = 0;
  DMM_VERIFY(dpu_get_nr_dpus(set, &actual_dpus));
  assert(actual_dpus == nr_dpus);

  const uint32_t k_pad = align_up_u32(k, 2);
  uint32_t max_rows = 0;
  dpu_rows_t *layout = calloc(nr_dpus, sizeof(*layout));
  llama_gemv_f32_args_t *args = calloc(nr_dpus, sizeof(*args));
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

  float *a = calloc((size_t)n * k, sizeof(float));
  float *x = calloc(k_pad, sizeof(float));
  float *y_ref = calloc(n, sizeof(float));
  float *y_dpu_padded = calloc((size_t)nr_dpus * max_rows, sizeof(float));
  float *a_padded = calloc((size_t)nr_dpus * a_floats_per_dpu, sizeof(float));
  assert(a != NULL && x != NULL && y_ref != NULL && y_dpu_padded != NULL &&
         a_padded != NULL);

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
             a + (size_t)global_row * k, k * sizeof(float));
    }
    args[d] = (llama_gemv_f32_args_t){
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
  DPU_FOREACH(set, each, idx) { DMM_VERIFY(dpu_prepare_xfer(each, args + idx)); }
  DMM_VERIFY(dpu_push_xfer(set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0,
                           sizeof(llama_gemv_f32_args_t), DPU_XFER_DEFAULT));

  idx = 0;
  DPU_FOREACH(set, each, idx) {
    DMM_VERIFY(dpu_prepare_xfer(each,
                                a_padded + (size_t)idx * a_floats_per_dpu));
  }
  DMM_VERIFY(dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                           a_bytes, DPU_XFER_DEFAULT));

  DMM_VERIFY(dpu_broadcast_to(set, DPU_MRAM_HEAP_POINTER_NAME, a_bytes, x,
                              x_bytes, DPU_XFER_DEFAULT));

  DMM_VERIFY(dpu_launch(set, DPU_SYNCHRONOUS));

  idx = 0;
  DPU_FOREACH(set, each, idx) {
    DMM_VERIFY(
        dpu_prepare_xfer(each, y_dpu_padded + (size_t)idx * max_rows));
  }
  DMM_VERIFY(dpu_push_xfer(set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                           a_bytes + x_bytes, y_bytes, DPU_XFER_DEFAULT));

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

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s /path/to/LLAMA_GEMV_F32\n", argv[0]);
    return 2;
  }

  const char *dpu_binary = argv[1];
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
