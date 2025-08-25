#include "moredefs.h"
#include <alloc.h>
#include <barrier.h>
#include <mram.h>
#include <mutex.h>

__host uint32_t nrWord;
__host uint32_t dpu_glob_result;
ALL_THREADS_BARRIER_INIT();
uint32_t localRes[NR_TASKLETS];

int main() {
  __mram_ptr const uint32_t *dat = DPU_MRAM_HEAP_POINTER;
  uint32_t nrWordTl = (nrWord + NR_TASKLETS - 1) / NR_TASKLETS;
  uint32_t start = nrWordTl * me();
  uint32_t end = start + nrWordTl;
  if (end > nrWord)
    end = nrWord;

  uint64_t localSum = 0;
  uint32_t wramCache[128];

  // Process in chunks of 128 words
  for (uint32_t i = start; i + 128 <= end; i += 128) {
    mram_read(&dat[i], wramCache, 128 * sizeof(uint32_t));
    for (int j = 0; j < 128; j++) {
      localSum += wramCache[j];
    }
  }

  // Handle remaining words
  uint32_t remaining = end - ((end - start) / 128) * 128 - start;
  if (remaining > 0) {
    uint32_t lastChunkStart = end - remaining;
    mram_read(&dat[lastChunkStart], wramCache, remaining * sizeof(uint32_t));
    for (uint32_t j = 0; j < remaining; j++) {
      localSum += wramCache[j];
    }
  }
  localRes[me()] = localSum;

  all_threads_barrier_wait();
  if (me() == 0) {
    for (uint32_t i = 0; i < NR_TASKLETS; ++i)
      dpu_glob_result += localRes[i];
  }

  return 0;
}
