#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>

__host uint32_t nrWord, initSum = ~0, global_dpu_red;
BARRIER_INIT(myBarr, NR_TASKLETS);
uint32_t global_tl_red[NR_TASKLETS];

int main() {
  __mram_ptr uint32_t *dat = DPU_MRAM_HEAP_POINTER;
  uint32_t nrWordTl = (nrWord + NR_TASKLETS - 1) / NR_TASKLETS;
  uint32_t start = nrWordTl * me();
  uint32_t end = start + nrWordTl;
  if (end > nrWord)
    end = nrWord;
  uint32_t localSum = 0;
  uint32_t wramCache[128];

  if (initSum == ~0) {
    // -- UPSWEEP (REDUCTION) --
    // Process in chunks of 128 words
    for (uint32_t i = start; i + 128 <= end; i += 128) {
      mram_read(&dat[i], wramCache, 128 * sizeof(uint32_t));
      for (int j = 0; j < 128; j++)
        localSum += wramCache[j];
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
    global_tl_red[me()] = localSum;
    barrier_wait(&myBarr);
    if (me() == 0) {
      global_dpu_red = 0;
      for (uint32_t i = 0; i < NR_TASKLETS; ++i)
        global_dpu_red += global_tl_red[i];
    }
    return 0;
  }

  // -- SPINE --
  // get prefix sum of all previous tasklets
  localSum = 0;
  for (uint32_t i = 0; i < me(); ++i)
    localSum += global_tl_red[i];

  // -- DOWNSWEEP --
  uint32_t runningSum = localSum + initSum;
  // Process in chunks of 128 words
  for (uint32_t i = start; i + 128 <= end; i += 128) {
    mram_read(&dat[i], wramCache, 128 * sizeof(uint32_t));
    for (int j = 0; j < 128; j++) {
      uint32_t temp = wramCache[j];
      wramCache[j] = runningSum + temp;
      runningSum += temp;
    }
    mram_write(wramCache, &dat[i], 128 * sizeof(uint32_t));
  }
  // Handle remaining words
  uint32_t remaining = end - ((end - start) / 128) * 128 - start;
  if (remaining > 0) {
    uint32_t lastChunkStart = end - remaining;
    mram_read(&dat[lastChunkStart], wramCache, remaining * sizeof(uint32_t));
    for (uint32_t j = 0; j < remaining; j++) {
      uint32_t temp = wramCache[j];
      wramCache[j] = runningSum + temp;
      runningSum += temp;
    }
    mram_write(wramCache, &dat[lastChunkStart], remaining * sizeof(uint32_t));
  }

  return 0;
}
