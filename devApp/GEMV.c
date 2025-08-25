#include <alloc.h>
#include <mram.h>
#include <defs.h>
#include <stddef.h>
#include <stdint.h>

#define VECTOR_SIZE 2048
// Host inputs
__host uint32_t num_rows; // Number of matrix rows for this DPU
__host int32_t input_vector[VECTOR_SIZE]; // Input vector (broadcast to all DPUs)
// MRAM matrix data (dynamic allocation)
__mram_ptr int32_t *matrix_rows = (__mram_ptr int32_t *)DPU_MRAM_HEAP_POINTER;
// Output results
__host int32_t result_vector[1024]; // Max rows per DPU (adjust as needed)

int main() {
  uint32_t tasklet_id = me();
  uint32_t num_tasklets = NR_TASKLETS;
  // No sync needed! :)
  // Calculate work distribution among tasklets
  uint32_t rows_per_tasklet = num_rows / num_tasklets;
  uint32_t remaining_rows = num_rows % num_tasklets;
  uint32_t start_row = tasklet_id * rows_per_tasklet;
  uint32_t my_rows = rows_per_tasklet;

  // Distribute remainder among first tasklets
  if (tasklet_id < remaining_rows) {
    start_row += tasklet_id;
    my_rows++;
  } else {
    start_row += remaining_rows;
  }
  uint32_t end_row = start_row + my_rows;

  // WRAM buffers for DMA
  int32_t matrix_row_buffer[128]; // Chunk of matrix row (128 * 4 = 512 bytes)
  // Process assigned rows
  for (uint32_t row = start_row; row < end_row; row++) {
    // Compute dot product: matrix_row · vector in chunks
    int32_t sum = 0;
    for (uint32_t chunk_start = 0; chunk_start < VECTOR_SIZE; chunk_start += 128) {
      // Load matrix row chunk from MRAM to WRAM
      mram_read(&matrix_rows[row * VECTOR_SIZE + chunk_start], matrix_row_buffer,
                128 * sizeof(int32_t));
      // Compute partial dot product for this chunk
      for (uint32_t i = 0; i < 128; i++)
        sum += matrix_row_buffer[i] * input_vector[chunk_start + i];
    }
    result_vector[row] = sum;
  }

  return 0;
}
