#include <mutex.h>
#include <defs.h>
#include <mram.h>
#include <alloc.h>

// Each DPU contains its own set of global variables
__host uint32_t nrWord = 1, res = 0x12345678;
// UPMEM Device Runtime provide mutexex, barriers,
// semaphores and handshakes for thread syncing
MUTEX_INIT(resMut);

int main() {
  // Reset the result each DPU launch
  res = 0;
  size_t nrWordTl = (nrWord + NR_TASKLETS - 1) / NR_TASKLETS;
  size_t resTl = 0;
  __mram_ptr const uint32_t *dat = DPU_MRAM_HEAP_POINTER;
  dat += nrWordTl * me();
  if (me() == NR_TASKLETS - 1)
    nrWordTl = nrWord - nrWordTl * me() - 1;
  for (size_t i = 0; i < nrWordTl; ++i)
    if (dat[i + 1] != dat[i])
      ++resTl;
  mutex_lock(resMut);
  res += resTl;
  mutex_unlock(resMut);
  return 0;
}
