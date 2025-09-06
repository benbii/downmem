#include <assert.h>
#include <dpu.h>
#include <stdlib.h>

// Comparator function for qsort
int compare_uint32(const void *a, const void *b) {
  uint32_t ua = *(const uint32_t*)a;
  uint32_t ub = *(const uint32_t*)b;
  return (ua > ub) - (ua < ub);
}

// Host-side binary search for verification (upper bound, matches DPU behavior)
size_t bs_host(uint32_t *arr, size_t n, uint32_t needle) {
  size_t left = 0, right = n;
  while (left < right) {
    size_t mid = (left + right) >> 1;
    if (arr[mid] < needle)
      left = mid + 1;
    else
      right = mid;
  }
  return right; // Return upper bound position
}

int main(int argc, char **argv) {
  srand(123456789);
  size_t nrDpu = atoi(argv[2]), nrHstk = atoi(argv[1]), nrNdl = nrDpu * 64, i;
  nrHstk = (nrHstk + nrDpu - 1) / nrDpu * nrDpu;
  uint32_t *hstks = malloc(nrHstk * sizeof(uint32_t));
  const uint32_t nrHstkDpu = nrHstk / nrDpu;
  struct dpu_set_t set, eachDpu;
  DPU_ASSERT(dpu_alloc(nrDpu, NULL, &set));
  DPU_ASSERT(dpu_load(set, argv[3], NULL));

  // Transfer haystack offset for each DPU first
  DPU_FOREACH(set, eachDpu, i) {
    hstks[i] = nrHstkDpu * i;
    dpu_prepare_xfer(eachDpu, hstks + i);
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "hstIdOff", 0, 4, DPU_XFER_DEFAULT));

  // Populate haystacks with random sorted data
  for (i = 0; i < nrHstk; ++i)
    hstks[i] = rand() % (nrHstk * 10); // Random values, range 0 to 10*nrHst
  // Sort the haystack array
  qsort(hstks, nrHstk, sizeof(uint32_t), compare_uint32);
  DPU_FOREACH(set, eachDpu) { dpu_prepare_xfer(eachDpu, &nrHstkDpu); }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "nrHst", 0, 4, DPU_XFER_DEFAULT));
  DPU_FOREACH(set, eachDpu, i) { dpu_prepare_xfer(eachDpu, hstks + i * nrHstkDpu); }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "hst", 0,
                           sizeof(uint32_t) * nrHstkDpu, DPU_XFER_DEFAULT));

  // Assuming that each DPU finds <128 results each iteration. This will lead
  // to some results not transferred and verified.
  uint32_t *reses = malloc(2 * sizeof(uint32_t) * 128 * nrDpu), ndls[1024];
  for (size_t j = 0; j < nrNdl; j += 1024) {
    // Generate needles - mix of existing values and random values
    for (i = 0; i < 1024; ++i) {
      if (i % 2 == 0 && nrHstk > 0) {
        // 50% chance: pick existing value from haystack
        ndls[i] = hstks[rand() % nrHstk];
      } else {
        // 50% chance: random value (may not exist)
        ndls[i] = rand() % (nrHstk * 10);
      }
    }

    DPU_FOREACH(set, eachDpu, i) { dpu_prepare_xfer(eachDpu, ndls); }
    DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "ndl", 0, 4 * 1024,
                             DPU_XFER_DEFAULT));
    DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

    DPU_FOREACH(set, eachDpu, i) { dpu_prepare_xfer(eachDpu, &reses[i * 2 * 128]); }
    DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                             0, 2 * sizeof(uint32_t) * 128, DPU_XFER_DEFAULT));

    for (size_t x = 0; x < nrDpu; ++x) {
      uint32_t *resEachDpu = &reses[x * 2 * 128];
      for (size_t resAt = 0; resAt < 2 * 128; resAt += 2) {
        const uint32_t ndl = resEachDpu[resAt], dpos = resEachDpu[resAt + 1];
        if (dpos == (uint32_t)-1)
          break;
        const size_t pos = bs_host(hstks, nrHstk, ndl);
        if (pos != dpos)
          return fprintf(stderr,
                         "bad at dpu %zu needle %u hostres %zu dpures %u\n", x,
                         ndl, pos, dpos);
      }
    }
  }

  // dpu_log_read(set, stdout);
  dpu_free(set); free(reses); free(hstks);
  return 0;
}
