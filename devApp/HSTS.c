#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <stddef.h>
#include <stdint.h>
#define NUM_BINS 16

// Host inputs
__host uint32_t num_elements;
// Global shared data structures (in WRAM)
__host uint32_t global_bins[NUM_BINS];

// MRAM input data (dynamic allocation)
__mram_ptr uint32_t *input_buffer = (__mram_ptr uint32_t *)&__sys_used_mram_end;

// One mutex per bin
mtx_t m[NUM_BINS];
_Static_assert(NUM_BINS < 32, "only <32 bins supported (1 mutex per bin)");

int main() {
  uint32_t tasklet_id = me();
  uint32_t num_tasklets = NR_TASKLETS;
  // No per-tasklet local histogram in HST-S
  // Initialize global histogram (not needed if only running once)
  for (uint32_t i = tasklet_id; i < NUM_BINS; i += NR_TASKLETS) {
    global_bins[i] = 0;
    m[i] = DPU_SPINMUTEX_INITIALIZER(i);
  }
  // Synchronize all tasklets
  all_threads_barrier_wait();

  // Calculate work distribution (simplified - num_elements is per-DPU)
  uint32_t elem_per_tl = num_elements / num_tasklets;
  uint32_t rem = num_elements % num_tasklets;
  uint32_t start_idx, my_elements;
  if (tasklet_id < rem) {
    // First 'rem' tasklets get one extra element
    my_elements = elem_per_tl + 1;
    start_idx = tasklet_id * my_elements;
  } else {
    // Remaining tasklets get standard amount
    my_elements = elem_per_tl;
    start_idx = rem + elem_per_tl * tasklet_id;
  }
  uint32_t end_idx = start_idx + my_elements;

  // Process local data and build local histogram
  uint32_t wram_buffer[128]; // WRAM buffer for DMA (512 bytes)
  for (uint32_t base = start_idx; base < end_idx; base += 128) {
    uint32_t chunk_size = (base + 128 <= end_idx) ? 128 : (end_idx - base);
    // DMA load chunk from MRAM to WRAM
    mram_read(input_buffer + base, wram_buffer, chunk_size * sizeof(uint32_t));
    // Process chunk
    for (uint32_t i = 0; i < chunk_size; i++) {
      uint32_t value = wram_buffer[i];
      uint32_t bin = value / 65536;
      if (bin >= NUM_BINS)
        bin = NUM_BINS - 1; // Handle edge case
      mutex_lock(m[bin]);
      global_bins[bin]++;
      mutex_unlock(m[bin]);
    }
  }

  // No aggregation at the end of program
  return 0;
}
