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

int main(int argc, char **argv) {
  srand(123456789);
  size_t nrDpu = atoi(argv[2]), nrHstk = atoi(argv[1]), nrNdl = nrDpu * 100, i;
  nrHstk = (nrHstk + nrDpu - 1) / nrDpu * nrDpu;
  uint32_t *hstks = malloc(nrHstk * sizeof(uint32_t));
  uint32_t *ndls = malloc(nrNdl * sizeof(uint32_t));
  uint32_t *hostres = malloc(nrNdl * sizeof(uint32_t));

  // ----- CPU RUN ------
  // Populate haystacks with random sorted data
  for (i = 0; i < nrHstk; ++i)
    hstks[i] = rand() % (nrHstk * 10); // Random values, range 0 to 10*nrHst
  // Sort the haystack array
  qsort(hstks, nrHstk, sizeof(uint32_t), compare_uint32);
  // Populate needles - mix of existing values and random values
  for (i = 0; i < nrNdl; ++i) {
    if (i % 2 == 0) {
      // 50% chance: pick existing value from haystack
      ndls[i] = hstks[rand() % nrHstk];
    } else {
      // 50% chance: random value (may not exist)
      ndls[i] = rand() % (nrHstk * 10);
    }
    hostres[i] = bs_run(hstks, nrHstk, ndls[i]);
  }


  // ----- DPU RUN ------
  const uint32_t nrHstkDpu = nrHstk / nrDpu;
  struct dpu_set_t set, eachDpu;
  DPU_ASSERT(dpu_alloc(nrDpu, NULL, &set));
  DPU_ASSERT(dpu_load(set, argv[3], NULL));

  // Transfer haystack offset for each DPU first
  uint32_t hstkOffs[nrDpu];
  DPU_FOREACH(set, eachDpu, i) {
    hstkOffs[i] = nrHstkDpu * i;
    dpu_prepare_xfer(eachDpu, hstkOffs + i);
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "hstkOff", 0, 4, DPU_XFER_DEFAULT));

  DPU_FOREACH(set, eachDpu) { dpu_prepare_xfer(eachDpu, &nrHstkDpu); }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "nrHst", 0, 4, DPU_XFER_DEFAULT));
  DPU_FOREACH(set, eachDpu, i) { dpu_prepare_xfer(eachDpu, hstks + i * nrHstkDpu); }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "hst", 0,
                           sizeof(uint32_t) * nrHstkDpu, DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_broadcast_to(set, "nrNdl", 0, &nrNdl, sizeof(uint32_t),
                              DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_broadcast_to(set, "ndl", 0, ndls, nrNdl * sizeof(uint32_t),
                              DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

  // ----- VERIFICATION ------
  // Assuming that each DPU finds <128 results. This will lead to some results
  // not transferred and verified, although the chances are low since nrNdl =
  // nrDpu * 100.
  uint32_t *reses = malloc(2 * sizeof(uint32_t) * 128 * nrDpu);
  DPU_FOREACH(set, eachDpu, i) { dpu_prepare_xfer(eachDpu, &reses[i * 2 * 128]); }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                           0, 2 * sizeof(uint32_t) * 128, DPU_XFER_DEFAULT));
  for (size_t resAt = 0; resAt < 2 * 128 * nrDpu; resAt += 2) {
    const uint32_t ndlId = reses[resAt], dpos = reses[resAt + 1];
    if (dpos == (uint32_t)-1) {
      resAt = (resAt / 256 + 1) * 256; // Next dpu
      continue;
    }
    const size_t pos = hostres[ndlId];
    if (hstks[dpos] != hstks[pos])
      return fprintf(stderr, "bad needle %u %u hostres %zu dpures %u\n", ndlId,
                     ndls[ndlId], pos, dpos);
  }

  dpu_free(set); free(reses); free(hstks);
  return 0;
}
