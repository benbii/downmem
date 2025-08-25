#include <assert.h>
#include <dpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define VECTOR_SIZE 2048

// Sparse matrix element: (row, col, value)
typedef struct {
  uint16_t row;
  uint16_t col;
  int32_t value;
} sparse_element_t;

int main(int argc, char **argv) {
  uint32_t matrix_rows = atoi(argv[1]);
  uint32_t num_dpus = atoi(argv[2]);
  const char *dpu_binary = argv[3];
  // Initialize DPU system
  struct dpu_set_t set, dpu;
  uint32_t each_dpu;
  DPU_ASSERT(dpu_alloc(num_dpus, NULL, &set));
  DPU_ASSERT(dpu_load(set, dpu_binary, NULL));

  // Generate sparse matrix and dense vector
  srand(123456789);
  // Generate vector
  int32_t *vector = malloc(VECTOR_SIZE * sizeof(int32_t));
  assert(vector != NULL);
  for (uint32_t i = 0; i < VECTOR_SIZE; i++) {
    vector[i] = rand() % 100 - 50; // Range [-50, 49]
  }
  // Generate sparse matrix elements
  uint32_t total_elements = 0;
  uint32_t *row_sizes = malloc(matrix_rows * sizeof(uint32_t));
  assert(row_sizes != NULL);
  // First pass: determine matrix size
  for (uint32_t row = 0; row < matrix_rows; row++) {
    row_sizes[row] = rand() % 1001; // 0-1000 elements per row
    total_elements += row_sizes[row];
  }
  // Pad total elements to multiple of num_dpus for bulk transfer
  uint32_t padded_elements =
      ((total_elements + num_dpus - 1) / num_dpus) * num_dpus;
  uint32_t elements_per_dpu = padded_elements / num_dpus;
  // Generate sparse matrix
  sparse_element_t *matrix = malloc(padded_elements * sizeof(sparse_element_t));
  assert(matrix != NULL);
  uint32_t elem_idx = 0;
  for (uint32_t row = 0; row < matrix_rows; row++) {
    for (uint32_t i = 0; i < row_sizes[row]; i++) {
      matrix[elem_idx].row = row;
      matrix[elem_idx].col = rand() % VECTOR_SIZE;
      matrix[elem_idx].value = rand() % 100 - 50; // Range [-50, 49]
      elem_idx++;
    }
  }
  free(row_sizes);
  // Pad with zero elements
  for (uint32_t i = total_elements; i < padded_elements; i++) {
    matrix[i].row = 0;
    matrix[i].col = 0;
    matrix[i].value = 0;
  }

  // Compute reference result on host
  int32_t *host_result = calloc(matrix_rows, sizeof(int32_t));
  assert(host_result != NULL);
  for (uint32_t i = 0; i < total_elements; i++) {
    uint16_t row = matrix[i].row;
    uint16_t col = matrix[i].col;
    int32_t value = matrix[i].value;
    host_result[row] += value * vector[col];
  }

  // Broadcast vector to all DPUs
  DPU_ASSERT(dpu_broadcast_to(set, "input_vector", 0, vector,
                              VECTOR_SIZE * sizeof(int32_t), DPU_XFER_DEFAULT));
  free(vector);
  // Broadcast elements_per_dpu to all DPUs
  DPU_ASSERT(dpu_broadcast_to(set, "num_elements", 0, &elements_per_dpu,
                              sizeof(uint32_t), DPU_XFER_DEFAULT));
  // Distribute matrix elements across DPUs
  uint32_t elem_offset = 0;
  DPU_FOREACH(set, dpu, each_dpu) {
    dpu_prepare_xfer(dpu, &matrix[elem_offset]);
    elem_offset += elements_per_dpu;
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "__sys_used_mram_end", 0,
                           elements_per_dpu * sizeof(sparse_element_t),
                           DPU_XFER_DEFAULT));
  free(matrix);
  DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

  // Collect results using simplified bulk transfer (TODO: use bulk xfer)
  printf("TODO: use bulk xfer...\n");
  int32_t *dpu_result = calloc(matrix_rows, sizeof(int32_t));
  assert(dpu_result != NULL);
  DPU_FOREACH(set, dpu, each_dpu) {
    uint32_t first_row;
    int32_t result_sums[64]; // MAX_ROWS_PER_DPU
    // Get first_row and all 64 result sums
    DPU_ASSERT(dpu_copy_from(dpu, "first_row", 0, &first_row, sizeof(uint32_t)));
    DPU_ASSERT(dpu_copy_from(dpu, "result_sums", 0, result_sums, 64 * sizeof(int32_t)));
    // Add all results unconditionally
    for (uint16_t i = 0; i < 64; i++) {
      uint16_t row_id = first_row + i;
      if (row_id < matrix_rows) {
        dpu_result[row_id] += result_sums[i];
      }
    }
  }

  for (uint32_t i = 0; i < matrix_rows; i++) {
    if (host_result[i] != dpu_result[i]) {
      return fprintf(stderr, "Row %u: Host=%d, DPU=%d\n", i, host_result[i],
                    dpu_result[i]);
    }
  }
  // Cleanup
  free(host_result); free(dpu_result);
  DPU_ASSERT(dpu_free(set));
  return 0;
}
