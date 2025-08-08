/**
 * app.c
 * MLP Host Application Source File
 *
 */

#include <assert.h>
#include <dpu.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t n_size;
  uint32_t n_size_pad;
  uint32_t nr_rows;
  uint32_t max_rows;
} dpu_arguments_t;
struct dpu_info_t {
  uint32_t rows_per_dpu;
  uint32_t rows_per_dpu_pad;
  uint32_t prev_rows_dpu;
};
struct dpu_info_t *dpu_info;
#define NUM_LAYERS 3
#define T int32_t

static T **A;
static T *B;
static T *B_host;
static T *B_tmp;
static T *C;
static T *C_dpu;

// Create input arrays
static void init_data(T **A, T *B, T *B_host, unsigned int m_size,
                      unsigned int n_size) {
  for (unsigned int l = 0; l < NUM_LAYERS; l++)
    for (unsigned int i = 0; i < m_size * n_size; i++) {
      if (i % 100 < 98) {
        A[l][i] = 0;
      } else {
        A[l][i] = (l + i) % 2;
      }
    }
  for (unsigned int i = 0; i < n_size; i++) {
    if (i % 50 < 48) {
      B[i] = 0;
    } else {
      B[i] = i % 2;
    }
    B_host[i] = B[i];
  }
}

// Compute output in the host
static void mlp_host(T *C, T **A, T *B, unsigned int m_size,
                     unsigned int n_size) {

  for (unsigned int nl = 0; nl < NUM_LAYERS; nl++) {
    for (unsigned int m = 0; m < m_size; m++) {
      C[m] = 0;
    }
    for (unsigned int m = 0; m < m_size; m++) {
      for (unsigned int n = 0; n < n_size; n++) {
        C[m] += A[nl][m * n_size + n] * B[n];
      }
#define max(x, y) (x > y ? x : y)
      C[m] = max(0, C[m]);
    }
    for (unsigned int n = 0; n < n_size; n++) {
      B[n] = C[n];
    }
  }
}

// Main of the Host Application
int main(int argc, char **argv) {
  struct dpu_set_t dpu_set, dpu;
  uint32_t nr_of_dpus;

  // Allocate DPUs and load binary
  DPU_ASSERT(dpu_alloc(atoi(argv[2]), NULL, &dpu_set));
  DPU_ASSERT(
      dpu_load(dpu_set, argv[3], NULL));
  DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &nr_of_dpus));

  unsigned int i, l;
  unsigned int m_size = atoi(argv[1]);
  unsigned int n_size = m_size;

  // Initialize help data
  dpu_info =
      (struct dpu_info_t *)malloc(nr_of_dpus * sizeof(struct dpu_info_t));
  dpu_arguments_t *input_args =
      (dpu_arguments_t *)malloc(nr_of_dpus * sizeof(dpu_arguments_t));
  uint32_t max_rows_per_dpu = 0;
  uint32_t n_size_pad = n_size;
  if (n_size % 2 == 1) {
    n_size_pad++;
  }

  i = 0;
  DPU_FOREACH(dpu_set, dpu, i) {
    uint32_t rows_per_dpu;
    uint32_t prev_rows_dpu = 0;
    uint32_t chunks = m_size / nr_of_dpus;
    rows_per_dpu = chunks;
    uint32_t rest_rows = m_size % nr_of_dpus;
    if (i < rest_rows)
      rows_per_dpu++;
    if (rest_rows > 0) {
      if (i >= rest_rows)
        prev_rows_dpu = rest_rows * (chunks + 1) + (i - rest_rows) * chunks;
      else
        prev_rows_dpu = i * (chunks + 1);
    } else {
      prev_rows_dpu = i * chunks;
    }

    // Keep max rows for parallel transfers
    uint32_t rows_per_dpu_pad = rows_per_dpu;
    if (rows_per_dpu_pad % 2 == 1) // 4-byte elements
      rows_per_dpu_pad++;
    if (rows_per_dpu_pad > max_rows_per_dpu)
      max_rows_per_dpu = rows_per_dpu_pad;

    dpu_info[i].rows_per_dpu = rows_per_dpu;
    dpu_info[i].rows_per_dpu_pad = rows_per_dpu_pad;
    dpu_info[i].prev_rows_dpu = prev_rows_dpu;

    // Copy input arguments to DPU
    input_args[i].n_size = n_size;
    input_args[i].n_size_pad = n_size_pad;
    input_args[i].nr_rows = rows_per_dpu;
  }

  A = (T **)malloc(NUM_LAYERS * sizeof(T *));
  for (l = 0; l < NUM_LAYERS; l++)
    A[l] = (T *)malloc(max_rows_per_dpu * nr_of_dpus * n_size_pad * sizeof(T));

  B = (T *)malloc(n_size * sizeof(T));
  B_host = (T *)malloc(n_size * sizeof(T));
  C = (T *)malloc(m_size * sizeof(T));
  C_dpu = malloc(max_rows_per_dpu * nr_of_dpus * sizeof(T));
  B_tmp = malloc(max_rows_per_dpu * nr_of_dpus * sizeof(T));

  init_data(A, B, B_host, m_size, n_size);

  // Compute output on CPU (performance comparison and verification purposes)
  mlp_host(C, A, B_host, m_size, n_size);

  i = 0;
  // Copy input arguments to DPU
  DPU_FOREACH(dpu_set, dpu, i) {
    input_args[i].max_rows = max_rows_per_dpu;
    DPU_ASSERT(dpu_prepare_xfer(dpu, input_args + i));
  }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0,
                           sizeof(dpu_arguments_t), DPU_XFER_DEFAULT));

  // Copy input array and vector
  i = 0;
  DPU_FOREACH(dpu_set, dpu, i) {
    DPU_ASSERT(
        dpu_prepare_xfer(dpu, A[0] + dpu_info[i].prev_rows_dpu * n_size));
  }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                           0, max_rows_per_dpu * n_size_pad * sizeof(T),
                           DPU_XFER_DEFAULT));
  i = 0;
  DPU_FOREACH(dpu_set, dpu, i) { DPU_ASSERT(dpu_prepare_xfer(dpu, B)); }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                           max_rows_per_dpu * n_size_pad * sizeof(T),
                           n_size_pad * sizeof(T), DPU_XFER_DEFAULT));

  DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));

  for (int lay = 1; lay < NUM_LAYERS; lay++) {
    i = 0;

    // Copy C_dpu
    DPU_FOREACH(dpu_set, dpu, i) {
      DPU_ASSERT(dpu_prepare_xfer(dpu, C_dpu + i * max_rows_per_dpu));
    }
    DPU_ASSERT(dpu_push_xfer(
        dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
        max_rows_per_dpu * n_size_pad * sizeof(T) + n_size_pad * sizeof(T),
        max_rows_per_dpu * sizeof(T), DPU_XFER_DEFAULT));

    // B = C
    unsigned int n, j;
    i = 0;
    for (n = 0; n < nr_of_dpus; n++) {
      for (j = 0; j < dpu_info[n].rows_per_dpu; j++) {
        B_tmp[i] = C_dpu[n * max_rows_per_dpu + j];
        i++;
      }
    }
    i = 0;
    DPU_FOREACH(dpu_set, dpu, i) { DPU_ASSERT(dpu_prepare_xfer(dpu, B_tmp)); }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU,
                             DPU_MRAM_HEAP_POINTER_NAME,
                             max_rows_per_dpu * n_size_pad * sizeof(T),
                             n_size_pad * sizeof(T), DPU_XFER_DEFAULT));

    // Copy next matrix of weights
    i = 0;
    DPU_FOREACH(dpu_set, dpu, i) {
      DPU_ASSERT(
          dpu_prepare_xfer(dpu, A[lay] + dpu_info[i].prev_rows_dpu * n_size));
    }
    DPU_ASSERT(dpu_push_xfer(
        dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
        max_rows_per_dpu * n_size_pad * sizeof(T), DPU_XFER_DEFAULT));

    DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));

    // Retrieve results
    i = 0;
    DPU_FOREACH(dpu_set, dpu, i) {
      DPU_ASSERT(dpu_prepare_xfer(dpu, C_dpu + i * max_rows_per_dpu));
    }
    DPU_ASSERT(dpu_push_xfer(
        dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
        max_rows_per_dpu * n_size_pad * sizeof(T) + n_size_pad * sizeof(T),
        max_rows_per_dpu * sizeof(T), DPU_XFER_DEFAULT));
  }

  // Check output
  bool status = true;
  unsigned int n, j;
  i = 0;
  for (n = 0; n < nr_of_dpus; n++) {
    for (j = 0; j < dpu_info[n].rows_per_dpu; j++) {
      if (C[i] != C_dpu[n * max_rows_per_dpu + j]) {
        status = false;
        // printf("%d: %d -- %d\n", i, C[i], C_dpu[n * max_rows_per_dpu + j]);
      }
      i++;
    }
  }

  // Deallocation
  for (i = 0; i < NUM_LAYERS; i++)
    free(A[i]);
  free(A);
  free(B);
  free(C);
  free(C_dpu);
  DPU_ASSERT(dpu_free(dpu_set));
  return status ? 0 : -1;
}
