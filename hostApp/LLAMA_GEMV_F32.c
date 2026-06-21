#include <assert.h>
#include <dpu.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static float pattern_x(uint32_t out_col, uint32_t k_col) {
  const int v = (int)((out_col * 11u + k_col * 19u + 5u) % 29u) - 14;
  return (float)v * 0.0625f;
}

static void host_mul_mat(const float *a, const float *x, float *y,
                         uint32_t n, uint32_t k, uint32_t m) {
  for (uint32_t out_col = 0; out_col < m; ++out_col) {
    for (uint32_t row = 0; row < n; ++row) {
      float sum = 0.0f;
      for (uint32_t col = 0; col < k; ++col) {
        sum += a[row * k + col] * x[out_col * k + col];
      }
      y[(size_t)out_col * n + row] = sum;
    }
  }
}

static void host_scale_f32(const float *x, float *y, uint32_t n,
                           float scale, float bias) {
  for (uint32_t i = 0; i < n; ++i) {
    y[i] = x[i] * scale + bias;
  }
}

static float pattern_scale_x(uint32_t i) {
  const int v = (int)((i * 37u + 11u) % 41u) - 20;
  return (float)v * 0.03125f;
}

static int nearly_equal(float got, float expected) {
  const float diff = fabsf(got - expected);
  const float scale = fmaxf(1.0f, fabsf(expected));
  return diff <= 1.0e-4f || diff <= scale * 1.0e-4f;
}

static int run_case(uint32_t n, uint32_t k, uint32_t m, uint32_t nr_dpus,
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
  const uint32_t x_floats_per_dpu = k_pad * m;
  const uint32_t y_floats_per_dpu = max_rows * m;
  const uint32_t a_bytes = a_floats_per_dpu * sizeof(float);
  const uint32_t x_bytes = x_floats_per_dpu * sizeof(float);
  const uint32_t y_bytes = y_floats_per_dpu * sizeof(float);

  float *a = calloc((size_t)n * k, sizeof(float));
  float *x = calloc((size_t)m * k, sizeof(float));
  float *x_padded = calloc(x_floats_per_dpu, sizeof(float));
  float *y_ref = calloc((size_t)n * m, sizeof(float));
  float *y_dpu_padded = calloc((size_t)nr_dpus * y_floats_per_dpu, sizeof(float));
  float *a_padded = calloc((size_t)nr_dpus * a_floats_per_dpu, sizeof(float));
  assert(a != NULL && x != NULL && x_padded != NULL && y_ref != NULL &&
         y_dpu_padded != NULL && a_padded != NULL);

  for (uint32_t row = 0; row < n; ++row) {
    for (uint32_t col = 0; col < k; ++col) {
      a[row * k + col] = pattern_a(row, col);
    }
  }
  for (uint32_t out_col = 0; out_col < m; ++out_col) {
    for (uint32_t col = 0; col < k; ++col) {
      const float value = pattern_x(out_col, col);
      x[out_col * k + col] = value;
      x_padded[out_col * k_pad + col] = value;
    }
  }
  host_mul_mat(a, x, y_ref, n, k, m);

  for (uint32_t d = 0; d < nr_dpus; ++d) {
    for (uint32_t row = 0; row < layout[d].rows; ++row) {
      const uint32_t global_row = layout[d].row_offset + row;
      memcpy(a_padded + (size_t)d * a_floats_per_dpu + row * k_pad,
             a + (size_t)global_row * k, k * sizeof(float));
    }
    args[d] = (llama_gemv_f32_args_t){
        .opcode = LLAMA_DOWNMEM_OP_MUL_MAT_F32,
        .k = k,
        .k_pad = k_pad,
        .n_rows = layout[d].rows,
        .max_rows = max_rows,
        .m = m,
        .n_elems = 0,
        .max_elems = 0,
        .a_offset = 0,
        .x_offset = a_bytes,
        .y_offset = a_bytes + x_bytes,
        .scale = 0.0f,
        .bias = 0.0f,
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

  DMM_VERIFY(dpu_broadcast_to(set, DPU_MRAM_HEAP_POINTER_NAME, a_bytes,
                              x_padded, x_bytes, DPU_XFER_DEFAULT));

  DMM_VERIFY(dpu_launch(set, DPU_SYNCHRONOUS));

  idx = 0;
  DPU_FOREACH(set, each, idx) {
    DMM_VERIFY(dpu_prepare_xfer(each,
                                y_dpu_padded + (size_t)idx * y_floats_per_dpu));
  }
  DMM_VERIFY(dpu_push_xfer(set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                           a_bytes + x_bytes, y_bytes, DPU_XFER_DEFAULT));

  int ok = 1;
  for (uint32_t d = 0; d < nr_dpus; ++d) {
    for (uint32_t row = 0; row < layout[d].rows; ++row) {
      const uint32_t global_row = layout[d].row_offset + row;
      for (uint32_t out_col = 0; out_col < m; ++out_col) {
        const float got = y_dpu_padded[(size_t)d * y_floats_per_dpu +
                                       (size_t)row * m + out_col];
        const float expected = y_ref[(size_t)out_col * n + global_row];
        if (!nearly_equal(got, expected)) {
          fprintf(stderr,
                  "case n=%u k=%u m=%u dpus=%u row=%u out_col=%u expected=%g got=%g\n",
                  n, k, m, nr_dpus, global_row, out_col, expected, got);
          ok = 0;
          goto done;
        }
      }
    }
  }

done:
  free(a);
  free(x);
  free(x_padded);
  free(y_ref);
  free(y_dpu_padded);
  free(a_padded);
  free(layout);
  free(args);
  DMM_VERIFY(dpu_free(set));
  return ok ? 0 : 1;
}

static int run_scale_case(uint32_t n, uint32_t nr_dpus,
                          const char *dpu_binary,
                          float scale, float bias) {
  struct dpu_set_t set;

  DMM_VERIFY(dpu_alloc(nr_dpus, NULL, &set));
  DMM_VERIFY(dpu_load(set, dpu_binary, NULL));

  uint32_t actual_dpus = 0;
  DMM_VERIFY(dpu_get_nr_dpus(set, &actual_dpus));
  assert(actual_dpus == nr_dpus);

  dpu_rows_t *layout = calloc(nr_dpus, sizeof(*layout));
  llama_gemv_f32_args_t *args = calloc(nr_dpus, sizeof(*args));
  assert(layout != NULL && args != NULL);

  uint32_t max_elems = 0;
  for (uint32_t d = 0; d < nr_dpus; ++d) {
    const uint32_t base = n / nr_dpus;
    const uint32_t rest = n % nr_dpus;
    layout[d].rows = base + (d < rest ? 1u : 0u);
    layout[d].row_offset = d * base + (d < rest ? d : rest);
    if (layout[d].rows > max_elems) {
      max_elems = layout[d].rows;
    }
  }
  if (max_elems == 0) {
    max_elems = 1;
  }

  const uint32_t x_bytes = max_elems * sizeof(float);
  const uint32_t y_bytes = max_elems * sizeof(float);

  float *x = calloc(n, sizeof(float));
  float *x_padded = calloc((size_t)nr_dpus * max_elems, sizeof(float));
  float *y_ref = calloc(n, sizeof(float));
  float *y_dpu_padded = calloc((size_t)nr_dpus * max_elems, sizeof(float));
  assert(x != NULL && x_padded != NULL && y_ref != NULL && y_dpu_padded != NULL);

  for (uint32_t i = 0; i < n; ++i) {
    x[i] = pattern_scale_x(i);
  }
  host_scale_f32(x, y_ref, n, scale, bias);

  for (uint32_t d = 0; d < nr_dpus; ++d) {
    memcpy(x_padded + (size_t)d * max_elems,
           x + layout[d].row_offset,
           layout[d].rows * sizeof(float));
    args[d] = (llama_gemv_f32_args_t){
        .opcode = LLAMA_DOWNMEM_OP_SCALE_F32,
        .k = 0,
        .k_pad = 0,
        .n_rows = 0,
        .max_rows = 0,
        .m = 0,
        .n_elems = layout[d].rows,
        .max_elems = max_elems,
        .a_offset = 0,
        .x_offset = 0,
        .y_offset = x_bytes,
        .scale = scale,
        .bias = bias,
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
    DMM_VERIFY(dpu_prepare_xfer(each, x_padded + (size_t)idx * max_elems));
  }
  DMM_VERIFY(dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                           0, x_bytes, DPU_XFER_DEFAULT));

  DMM_VERIFY(dpu_launch(set, DPU_SYNCHRONOUS));

  idx = 0;
  DPU_FOREACH(set, each, idx) {
    DMM_VERIFY(dpu_prepare_xfer(each, y_dpu_padded + (size_t)idx * max_elems));
  }
  DMM_VERIFY(dpu_push_xfer(set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                           x_bytes, y_bytes, DPU_XFER_DEFAULT));

  int ok = 1;
  for (uint32_t d = 0; d < nr_dpus; ++d) {
    for (uint32_t i = 0; i < layout[d].rows; ++i) {
      const uint32_t global = layout[d].row_offset + i;
      const float got = y_dpu_padded[(size_t)d * max_elems + i];
      if (!nearly_equal(got, y_ref[global])) {
        fprintf(stderr,
                "scale case n=%u dpus=%u i=%u expected=%g got=%g\n",
                n, nr_dpus, global, y_ref[global], got);
        ok = 0;
        goto done;
      }
    }
  }

done:
  free(x);
  free(x_padded);
  free(y_ref);
  free(y_dpu_padded);
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
  failed |= run_case(8, 16, 1, 1, dpu_binary);
  failed |= run_case(17, 31, 1, 4, dpu_binary);
  failed |= run_case(17, 31, 3, 4, dpu_binary);
  failed |= run_case(33, 64, 5, 6, dpu_binary);
  failed |= run_case(3, 7, 4, 8, dpu_binary);
  failed |= run_scale_case(17, 4, dpu_binary, 1.25f, -0.5f);
  failed |= run_scale_case(64, 1, dpu_binary, -0.75f, 0.125f);
  failed |= run_scale_case(3, 8, dpu_binary, 0.5f, 2.0f);

  if (failed != 0) {
    fprintf(stderr, "LLAMA_GEMV_F32 failed\n");
    return 1;
  }

  printf("LLAMA_GEMV_F32 passed\n");
  return 0;
}
