#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <stddef.h>
#include <stdint.h>

#define NUM_BINS 16
#define MAX_VALUE (65536 * 16)
#define BINS_PER_MUTEX 4
#define NUM_MUTEXES 4

// Host inputs
__host uint32_t num_elements;
// Global shared data structures (in WRAM)
__host uint32_t global_bins[NUM_BINS];

// MRAM input data (dynamic allocation)
__mram_ptr uint32_t *input_buffer = (__mram_ptr uint32_t *)&__sys_used_mram_end;

MUTEX_INIT(m0); MUTEX_INIT(m1); MUTEX_INIT(m2); MUTEX_INIT(m3);

int main() {
  uint32_t tasklet_id = me();
  uint32_t num_tasklets = NR_TASKLETS;
  // Per-tasklet local histogram (on stack)
  uint32_t local_bins[NUM_BINS];
  // Initialize local histogram
  for (int i = 0; i < NUM_BINS; i++)
    local_bins[i] = 0;
  // Initialize global histogram (not needed if only running once)
  // for (int i = tasklet_id; i < NUM_BINS; i += NR_TASKLETS)
  //   global_bins[i] = 0;
  // Synchronize all tasklets
  // all_threads_barrier_wait();

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
      local_bins[bin]++;
    }
  }

  // Aggregate local histograms to global histogram
  // Use mutexes to protect groups of 4 bins each
  mutex_lock(m0);
  global_bins[0] += local_bins[0]; global_bins[1] += local_bins[1];
  global_bins[2] += local_bins[2]; global_bins[3] += local_bins[3];
  mutex_unlock(m0);
  mutex_lock(m1);
  global_bins[4] += local_bins[4]; global_bins[5] += local_bins[5];
  global_bins[6] += local_bins[6]; global_bins[7] += local_bins[7];
  mutex_unlock(m1);
  mutex_lock(m2);
  global_bins[8] += local_bins[8]; global_bins[9] += local_bins[9];
  global_bins[10] += local_bins[10]; global_bins[11] += local_bins[11];
  mutex_unlock(m2);
  mutex_lock(m3);
  global_bins[12] += local_bins[12]; global_bins[13] += local_bins[13];
  global_bins[14] += local_bins[14]; global_bins[15] += local_bins[15];
  mutex_unlock(m3);
  return 0;
}
