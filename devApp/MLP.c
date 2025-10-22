// PrIM's Matrix vector multiplication with multiple tasklet
#include <alloc.h>
#include <barrier.h>

typedef struct {
  uint32_t n_size, n_size_pad, nr_rows, max_rows;
} dpu_args_t;
__host dpu_args_t DPU_INPUT_ARGUMENTS;
#define BLOCK_SIZE_LOG2 8
#define BLOCK_SIZE (1 << BLOCK_SIZE_LOG2)
#define T int32_t

void __attribute__((noinline))
gemv(T *bufferC, T *bufferA, T *bufferB, int pos) {
  for (unsigned int i = 0; i < BLOCK_SIZE / sizeof(T); i++) 
    bufferC[pos] += bufferA[i] * bufferB[i];
}

ALL_THREADS_BARRIER_INIT();

int main() {
  unsigned int tasklet_id = me();
#if PRINT
  printf("tasklet_id = %u\n", tasklet_id);
#endif
  if (tasklet_id == 0) mem_reset();         
  all_threads_barrier_wait();

  int32_t n_size = DPU_INPUT_ARGUMENTS.n_size;
  int32_t n_size_pad = DPU_INPUT_ARGUMENTS.n_size_pad;
  uint32_t nr_rows = DPU_INPUT_ARGUMENTS.nr_rows;
  uint32_t max_rows = DPU_INPUT_ARGUMENTS.max_rows;
  unsigned int start_row;
  unsigned int chunks = nr_rows / (NR_TASKLETS + NR_TASKLETS);
  unsigned int dbl_chunks = chunks + chunks;
  unsigned int rows_per_tasklet = dbl_chunks;
  unsigned int remaining_rows = nr_rows % (NR_TASKLETS + NR_TASKLETS);

  if ((tasklet_id + tasklet_id) < remaining_rows) rows_per_tasklet += 2;
  if (remaining_rows > 0) {
    if ((tasklet_id + tasklet_id) >= remaining_rows) {
      unsigned int half_remaining = remaining_rows >> 1;
      if ((remaining_rows & 1) == 1) 
        start_row = (half_remaining + 1) * (dbl_chunks + 2) 
          + (tasklet_id - 1 - half_remaining) * dbl_chunks;
      else start_row = (half_remaining) * (dbl_chunks + 2) + (tasklet_id - half_remaining) * dbl_chunks;
    } else start_row = tasklet_id * (dbl_chunks + 2);
  } else start_row = tasklet_id * (dbl_chunks);

  // Address of the current row in MRAM
  uintptr_t mram_A = (uintptr_t)(DPU_MRAM_HEAP_POINTER + start_row * n_size * sizeof(T));
  uintptr_t mram_B = (uintptr_t)(DPU_MRAM_HEAP_POINTER + max_rows * n_size_pad * sizeof(T));
  uintptr_t mram_C = (uintptr_t)(DPU_MRAM_HEAP_POINTER + max_rows * n_size_pad * sizeof(T) +
                 n_size_pad * sizeof(T) + start_row * sizeof(T));
  // Inititalize a local cache to store the MRAM block
  T *cache_A = (T *)mem_alloc(BLOCK_SIZE + 8), *cache_A_aux = (T *)mem_alloc(8);
  T *cache_B = (T *)mem_alloc(BLOCK_SIZE), *cache_C = (T *)mem_alloc(8);
  int needs_offset = 0;
  // Iterate over nr_rows
  for (unsigned int i = start_row; i < start_row + rows_per_tasklet; i += 2) {
    uintptr_t current_A = (uintptr_t)(DPU_MRAM_HEAP_POINTER + i * n_size * sizeof(T));
    uintptr_t current_B = mram_B;
    cache_C[0] = 0; cache_C[1] = 0;

    for (unsigned int pos = 0; pos < 2 && i + pos < nr_rows; pos++) {
      int n = 0, j;
      for (n = 0; n < (int32_t)(n_size - (BLOCK_SIZE / sizeof(T)));
           n += (BLOCK_SIZE / sizeof(T))) {
        mram_read((__mram_ptr void const *)(current_A), cache_A, BLOCK_SIZE);
        mram_read((__mram_ptr void const *)(current_B), cache_B, BLOCK_SIZE);
        if (needs_offset) {
          for (unsigned int off = 0; off < (BLOCK_SIZE / sizeof(T)) - 1; off++) 
            cache_A[off] = cache_A[off + 1];
          mram_read((__mram_ptr void const *)(current_A + BLOCK_SIZE), cache_A_aux, 8);
          cache_A[BLOCK_SIZE / sizeof(T) - 1] = cache_A_aux[0];
        }
        gemv(cache_C, cache_A, cache_B, pos);
        current_A += BLOCK_SIZE; current_B += BLOCK_SIZE;
      }
      mram_read((__mram_ptr void const *)(current_A), cache_A, BLOCK_SIZE);
      if (needs_offset) {
        for (unsigned int off = 0; off < (BLOCK_SIZE / sizeof(T)) - 1; off++) 
          cache_A[off] = cache_A[off + 1];
        mram_read((__mram_ptr void const *)(current_A + BLOCK_SIZE), cache_A_aux, 8);
        cache_A[BLOCK_SIZE / sizeof(T) - 1] = cache_A_aux[0];
      }
      mram_read((__mram_ptr void const *)(current_B), cache_B, BLOCK_SIZE);
      for (j = 0; j < (int)(n_size - n); j++) 
        cache_C[pos] += cache_A[j] * cache_B[j];
      current_A += (BLOCK_SIZE - ((BLOCK_SIZE / sizeof(T)) - (n_size - n)) * sizeof(T));
      current_B = mram_B;
      needs_offset = (current_A % 8 != 0);
    }
    mram_write(cache_C, (__mram_ptr void *)(mram_C), 8);
    mram_C += 2 * sizeof(T);
  }
  return 0;
}
