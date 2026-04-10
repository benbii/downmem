#include "moredefs.h"
#include <alloc.h>
#include <handshake.h>
#include <mram.h>

__host uint32_t nrWord, initSum = ~0, dpuRed;
uint32_t tlRed[NR_TASKLETS];

int main() {
  __mram_ptr uint32_t *dat = DPU_MRAM_HEAP_POINTER;
  const uint32_t nrWordTl = (nrWord + NR_TASKLETS - 1) / NR_TASKLETS, my_id = me();
  const uint32_t start = nrWordTl * my_id;
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

    // No spine phase. Prim style handshake instead.
    tlRed[my_id] = localSum;
    if (my_id != 0) {
      handshake_wait_for(my_id - 1);
      tlRed[my_id] = tlRed[my_id - 1] + localSum;
    }
    if (my_id + 1 != NR_TASKLETS)
      handshake_notify();
    else dpuRed = tlRed[my_id];
    return 0;
  }

  // -- DOWNSWEEP --
  uint32_t runningSum = (my_id != 0 ? tlRed[my_id - 1] : 0) + initSum;
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
