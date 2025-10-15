#include "moredefs.h"
#include <alloc.h>
#include <mram.h>
#include <mutex.h>
#include <stddef.h>
#include <stdint.h>

// Host inputs
__host uint32_t num_elements;              // Number of elements for this DPU
// MRAM data locations
__mram_ptr uint32_t *input_data = (__mram_ptr uint32_t *)DPU_MRAM_HEAP_POINTER;
__mram_noinit uint32_t output_data[69420];
__host volatile uint32_t out_at; // current index at output_data, aka, nr. of outputs
MUTEX_INIT(mut_outat);
ALL_THREADS_BARRIER_INIT();

int main() {
  uint32_t tasklet_id = me();
  out_at = 0;
  all_threads_barrier_wait();

  // Calculate work distribution (ensure even number of elements per tasklet for
  // 8-byte alignment)
  uint32_t elem_per_tl = (num_elements + NR_TASKLETS - 1) / NR_TASKLETS;
  if (elem_per_tl % 2 == 1) elem_per_tl++;
  uint32_t start_elem = tasklet_id * elem_per_tl;
  uint32_t end_elem = start_elem + elem_per_tl;
  // Handle remaining elements for last tasklet
  if (end_elem > num_elements)
    end_elem = num_elements;

  // WRAM buffer for DMA reads (8-byte aligned)
  uint32_t read_buffer[96];
  // Local buffer for odd numbers (on stack)
  uint32_t local_count = 0, local_odds[96];

  for (uint32_t base = start_elem; base < end_elem; base += 96) {
    uint32_t chunk_size = (base + 96 <= end_elem) ? 96 : (end_elem - base);
    // 8B aligned because elem_per_tl is even
    mram_read(&input_data[base], read_buffer, chunk_size * sizeof(uint32_t));

    // Process each element in the chunk
    for (uint32_t i = 0; i < chunk_size && base + i < end_elem; i++) {
      uint32_t value = read_buffer[i];
      if (value % 2 == 0) continue;
      local_odds[local_count++] = value;
      if (local_count != 96) continue;

      mutex_lock(mut_outat);
      uint32_t my_out = out_at;
      out_at += 96;
      mutex_unlock(mut_outat);
      mram_write(local_odds, output_data + my_out, 96 * sizeof(uint32_t));
      local_count = 0;
    }
  }
  // Still don't know why this barrier is needed :(
  all_threads_barrier_wait();

  // Each thread write its leftover local_odds with padding for 8-byte alignment
  // Add padding zero if needed for even count (8-byte alignment)
  if (local_count % 2 == 1) {
    local_odds[local_count] = 0;
    local_count++;
  }
  if (local_count > 0) {
    mutex_lock(mut_outat);
    uint32_t my_out = out_at;
    out_at += local_count;
    mutex_unlock(mut_outat);
    mram_write(local_odds, output_data + my_out, local_count * sizeof(uint32_t));
  }
  return 0;
}
