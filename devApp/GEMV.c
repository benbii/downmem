#include "moredefs.h"
#include <alloc.h>
#include <barrier.h>
#include <mram.h>
#include <string.h>
#include <stdint.h>

#define VECTOR_SIZE 2048
// Host inputs
__host uint32_t num_rows; // Number of matrix rows for this DPU
// Input vector (broadcast to all DPUs)
__mram_noinit int32_t input_vector_m[VECTOR_SIZE];
// MRAM matrix data (dynamic allocation)
__mram_ptr int32_t *matrix_rows = (__mram_ptr int32_t *)DPU_MRAM_HEAP_POINTER;
// Output results
__mram_noinit int32_t result_vector_m[1024]; // Max rows per DPU (adjust as needed)
// xfer with mram, work with wram
static int32_t input_vector[VECTOR_SIZE], result_vector[1024];

ALL_THREADS_BARRIER_INIT();

int main() {
  // Calculate work distribution among tasklets
  const uint32_t remaining_rows = num_rows % NR_TASKLETS, me_ = me();
  uint32_t my_rows = num_rows / NR_TASKLETS, start_row = me_ * my_rows;
  // Host xfer with MRAM, DPU compute with wram
  uint32_t ldSz = VECTOR_SIZE / NR_TASKLETS;
  mram_read(input_vector_m + me_ * ldSz, input_vector + me_ * ldSz,
            ldSz * sizeof(int32_t));
  // Zero out results
  ldSz = 1024 / NR_TASKLETS;
  memset(result_vector, 0, ldSz * 4);
  all_threads_barrier_wait();

  // Distribute remainder among first tasklets
  if (me_ < remaining_rows) {
    start_row += me_;
    my_rows++;
  } else {
    start_row += remaining_rows;
  }
  const uint32_t end_row = start_row + my_rows;

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
  all_threads_barrier_wait();

  _Static_assert(1024 % NR_TASKLETS == 0, "1024 % NR_TASKLETS == 0");
  mram_write(result_vector + me_ * ldSz, result_vector_m + me_ * ldSz,
             ldSz * sizeof(int32_t));
  return 0;
}
