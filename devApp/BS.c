/*
* Binary Search with multiple tasklets
*
*/
#include <alloc.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>

// Needles and Haystack
__host uint32_t nth_kernel, nrHst, hstkOff, nrNdl;
__host uint32_t hst[8192];
__mram_noinit uint32_t ndl[1 << 20];
uint32_t resAt, nrFinish;
MUTEX_INIT(resAtMut);
MUTEX_INIT(nrFinishMut);

// matches CPU behavior
size_t bs_run(uint32_t *haystack, size_t nrHstk, uint32_t needle) {
  size_t left = 0, right = nrHstk;
  while (left < right) {
    size_t mid = (left + right) >> 1;
    if (haystack[mid] < needle)
      left = mid + 1;
    else
      right = mid;
  }
  return left;
}

int main() {
  resAt = 0, nrFinish = 0;
  const uint32_t min = hst[0], max = hst[nrHst - 1];
  __mram_ptr uint32_t *const reses = DPU_MRAM_HEAP_POINTER;

  for (size_t i = me(); i < nrNdl; i += NR_TASKLETS) {
    uint32_t curNdl = ndl[i];
    if (curNdl < min || curNdl > max)
      continue;

    uint32_t bruh[2] = {i, bs_run(hst, nrHst, curNdl) + hstkOff};
    mutex_lock(resAtMut);
    uint32_t myResAt = resAt;
    resAt += 2;
    mutex_unlock(resAtMut);
    mram_write(bruh, &reses[myResAt], 8);
  }

  mutex_lock(nrFinishMut);
  ++nrFinish;
  mutex_unlock(nrFinishMut);
  if (nrFinish == NR_TASKLETS) {
    uint32_t nulTerm[2] = {~0, ~0};
    mram_write(nulTerm, &reses[resAt], 8);
  }
  return resAt;
}
