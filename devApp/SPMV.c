#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <stddef.h>
#include <stdint.h>

#define VECTOR_SIZE 2048
#define MAX_ROWS_PER_DPU 64

// Sparse matrix element: (row, col, value)
typedef struct {
    uint16_t row;
    uint16_t col;
    int32_t value;
} sparse_element_t;

// Host inputs
__host uint32_t num_elements;              // Number of sparse elements for this DPU
__host int32_t input_vector[VECTOR_SIZE];  // Input vector (broadcast to all DPUs)
// MRAM sparse matrix data (dynamic allocation)
__mram_ptr sparse_element_t *matrix_elements =
    (__mram_ptr sparse_element_t *)DPU_MRAM_HEAP_POINTER;
// Output results (simplified: just array of sums indexed by row offset)
__host uint32_t first_row;          // First row this DPU processes
__host int32_t result_sums[MAX_ROWS_PER_DPU]; // Sums indexed by (row - first_row)
static int32_t local_sums[NR_TASKLETS][MAX_ROWS_PER_DPU];
BARRIER_INIT(allthrd, NR_TASKLETS);

int main() {
  uint32_t id = me();
  uint32_t num_tasklets = NR_TASKLETS;
  // Simple direct MRAM access does not hurt :)
  if (id == 0)
    first_row = matrix_elements[0].row;
  for (uint32_t i = id; i < MAX_ROWS_PER_DPU; i += NR_TASKLETS)
    result_sums[i] = 0;
  for (uint32_t i = 0; i < MAX_ROWS_PER_DPU; i += 1)
    local_sums[id][i] = 0;
  barrier_wait(&allthrd);

  // Calculate work distribution among tasklets
  uint32_t elements_per_tasklet = num_elements / num_tasklets;
  uint32_t remaining_elements = num_elements % num_tasklets;
  uint32_t start_elem = id * elements_per_tasklet;
  uint32_t my_elements = elements_per_tasklet;
  // Distribute remainder among first tasklets
  if (id < remaining_elements) {
    start_elem += id;
    my_elements++;
  } else {
    start_elem += remaining_elements;
  }
  uint32_t end_elem = start_elem + my_elements;

  // Local accumulator for this tasklet (force 0 init)
  // WRAM buffers for DMA (fit within stack limits)
  sparse_element_t element_buffer[64]; // 64 elements * 8 bytes = 512 bytes

  // Process assigned elements in chunks
  for (uint32_t base = start_elem; base < end_elem; base += 64) {
    uint32_t chunk_size = (base + 64 <= end_elem) ? 64 : (end_elem - base);
    // Load chunk of sparse elements from MRAM to WRAM
    mram_read(&matrix_elements[base], element_buffer,
              chunk_size * sizeof(sparse_element_t));

    // Process each element in the chunk
    for (uint32_t i = 0; i < chunk_size; i++) {
      sparse_element_t elem = element_buffer[i];
      // Skip zero elements (which also include padding)
      if (elem.value == 0)
        continue;
      int32_t product = elem.value * input_vector[elem.col];
      // Add to local sum array (offset by first row)
      uint32_t row_offset = elem.row - first_row;
      if (row_offset < MAX_ROWS_PER_DPU)
        local_sums[id][row_offset] += product;
    }
  }

  barrier_wait(&allthrd);
  // Merge local sums into global sums
  for (uint32_t i = 0; i < NR_TASKLETS; i++) {
    const uint32_t bruh = MAX_ROWS_PER_DPU / NR_TASKLETS;
    for (uint32_t j = bruh * id; j < bruh * (id + 1); j++)
      result_sums[j] += local_sums[i][j];
  }
  return 0;
}
