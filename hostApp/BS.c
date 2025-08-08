#include <assert.h>
#include <dpu.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  srand(123456789);
  size_t nrDpu = atoi(argv[2]), nrHst = atoi(argv[1]), nrNdl = nrDpu * 64, i;
  nrHst = (nrHst + nrDpu - 1) / nrDpu * nrDpu;
  uint32_t *hsts = malloc(nrHst * sizeof(uint32_t));
  uint32_t nrHstDpu = nrHst / nrDpu;
  struct dpu_set_t set, eachDpu;
  DPU_ASSERT(dpu_alloc(nrDpu, NULL, &set));
  DPU_ASSERT(dpu_load(set, argv[3], NULL));

  // Transfer haystack offset for each DPU first
  DPU_FOREACH(set, eachDpu, i) {
    hsts[i] = nrHstDpu * i;
    dpu_prepare_xfer(eachDpu, hsts + i);
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "hstIdOff", 0, 4, DPU_XFER_DEFAULT));

  // Populate haystacks
  for (i = 0; i < nrHst; ++i)
    hsts[i] = i;
  DPU_FOREACH(set, eachDpu) { dpu_prepare_xfer(eachDpu, &nrHstDpu); }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "nrHst", 0, 4, DPU_XFER_DEFAULT));
  DPU_FOREACH(set, eachDpu, i) { dpu_prepare_xfer(eachDpu, hsts + i * nrHstDpu); }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "hst", 0,
                           sizeof(uint32_t) * nrHstDpu, DPU_XFER_DEFAULT));

  // Assuming that each DPU finds <128 results each iteration. This will lead
  // to some results not transferred and verified.
  uint32_t *reses = malloc(2 * sizeof(uint32_t) * 128 * nrDpu), ndls[1024];
  for (size_t j = 0; j < nrNdl; j += 1024) {
    for (i = 0; i < 1024; ++i)
      ndls[i] = rand() % nrHst;
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
        if (resEachDpu[resAt] == -1)
          break;
        assert(resEachDpu[resAt] == resEachDpu[resAt + 1]);
      }
    }
  }

  // dpu_log_read(set, stdout);
  dpu_free(set); free(reses); free(hsts);
  // assert(false);
  return 0;
}
