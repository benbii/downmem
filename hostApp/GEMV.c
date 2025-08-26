#include <assert.h>
#include <dpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define VECTOR_SIZE 2048

int main(int argc, char **argv) {
  uint32_t matrix_rows = atoi(argv[1]);
  uint32_t num_dpus = atoi(argv[2]);
  const char *dpu_binary = argv[3];
  // Force matrix rows to be divisible by number of DPUs
  matrix_rows = (matrix_rows + num_dpus - 1) / num_dpus * num_dpus;
  uint32_t rows_per_dpu = matrix_rows / num_dpus;

  // Generate random matrix and vector
  int32_t *matrix = malloc(matrix_rows * VECTOR_SIZE * sizeof(int32_t));
  int32_t *vector = malloc(VECTOR_SIZE * sizeof(int32_t));
  int32_t *result = malloc(matrix_rows * sizeof(int32_t));
  assert(matrix != NULL && vector != NULL && result != NULL);
  srand(time(NULL));
  for (uint32_t i = 0; i < matrix_rows * VECTOR_SIZE; i++)
    matrix[i] = rand() % 100 - 50; // Range [-50, 49]
  for (uint32_t i = 0; i < VECTOR_SIZE; i++)
    vector[i] = rand() % 100 - 50; // Range [-50, 49]
  // Compute host-side GEMV for verification
  for (uint32_t row = 0; row < matrix_rows; row++) {
    int64_t sum = 0;
    for (uint32_t col = 0; col < VECTOR_SIZE; col++)
      sum += (int64_t)matrix[row * VECTOR_SIZE + col] * vector[col];
    result[row] = (int32_t)sum; // Truncate to int32_t
  }

  // Initialize DPU system
  struct dpu_set_t set, each;
  uint32_t idx_dpu;
  DPU_ASSERT(dpu_alloc(num_dpus, NULL, &set));
  DPU_ASSERT(dpu_load(set, dpu_binary, NULL));
  // Broadcast vector to all DPUs
  DPU_ASSERT(dpu_broadcast_to(set, "input_vector", 0, vector,
                              VECTOR_SIZE * sizeof(int32_t), DPU_XFER_DEFAULT));
  // Broadcast rows_per_dpu to all DPUs
  DPU_ASSERT(dpu_broadcast_to(set, "num_rows", 0, &rows_per_dpu,
                              sizeof(uint32_t), DPU_XFER_DEFAULT));
  // Distribute matrix rows across DPUs
  uint32_t row_offset = 0;
  DPU_FOREACH(set, each, idx_dpu) {
    int32_t *dpu_matrix_rows = &matrix[row_offset * VECTOR_SIZE];
    dpu_prepare_xfer(each, dpu_matrix_rows);
    row_offset += rows_per_dpu;
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "__sys_used_mram_end", 0,
                           rows_per_dpu * VECTOR_SIZE * sizeof(int32_t),
                           DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

  // Collect results from DPUs
  int32_t *dpu_result = malloc(matrix_rows * sizeof(int32_t));
  assert(dpu_result != NULL);
  row_offset = 0;
  DPU_FOREACH(set, each, idx_dpu) {
    dpu_prepare_xfer(each, &dpu_result[row_offset]);
    row_offset += rows_per_dpu;
  }
  dpu_push_xfer(set, DPU_XFER_FROM_DPU, "result_vector", 0,
                rows_per_dpu * sizeof(int32_t), DPU_XFER_DEFAULT);

  for (uint32_t i = 0; i < matrix_rows; i++)
    if (result[i] != dpu_result[i])
      return fprintf(stderr, "Row %u: Host=%d, DPU=%d\n", i, result[i],
                     dpu_result[i]);

  // Cleanup
  free(matrix); free(vector); free(result); free(dpu_result);
  DPU_ASSERT(dpu_free(set));
  return 0;
}
