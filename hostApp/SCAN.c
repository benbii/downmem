#include <assert.h>
#include <dpu.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  uint32_t nrWord = atoi(argv[1]), nrDpu = atoi(argv[2]);
  nrWord = ((nrWord + nrDpu - 1) / nrDpu) * nrDpu;
  uint32_t nrWordDpu = nrWord / nrDpu;
  uint32_t *dat = malloc(nrWord * sizeof(uint32_t));
  for (size_t i = 0; i < nrWord; ++i)
    dat[i] = 1; // easier for now
  // Calculate expected prefix sum for verification
  uint32_t *expected = malloc(nrWord * sizeof(uint32_t));
  expected[0] = dat[0];
  for (size_t i = 1; i < nrWord; ++i)
    expected[i] = expected[i-1] + dat[i];

  struct dpu_set_t set, eachDpu;
  DPU_ASSERT(dpu_alloc(nrDpu, NULL, &set));
  DPU_ASSERT(dpu_load(set, argv[3], NULL));
  DPU_ASSERT(dpu_broadcast_to(set, "nrWord", 0, &nrWordDpu, sizeof(uint32_t), DPU_XFER_DEFAULT));

  // FIRST RUN: in-dpu upsweep
  size_t i;
  DPU_FOREACH(set, eachDpu, i) {
    dpu_prepare_xfer(eachDpu, dat + i * nrWordDpu);
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                           nrWordDpu * sizeof(uint32_t), DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));
  // Read back the scanned results
  uint32_t dpuSpine[nrDpu];
  DPU_FOREACH(set, eachDpu, i) {
    dpu_prepare_xfer(eachDpu, dpuSpine + i);
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, "global_dpu_red", 0,
                           sizeof(uint32_t), DPU_XFER_DEFAULT));

  // spine phase on host
  uint32_t hostSpine[nrDpu];
  hostSpine[0] = 0;
  for (size_t i = 1; i < nrDpu; ++i) {
    hostSpine[i] = hostSpine[i-1] + dpuSpine[i-1];
  }

  // SECOND RUN: in-dpu downsweep
  DPU_FOREACH(set, eachDpu, i) {
    dpu_prepare_xfer(eachDpu, &hostSpine[i]);
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "initSum", 0, sizeof(uint32_t), DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

  // Read back the scanned results
  DPU_FOREACH(set, eachDpu, i) {
    dpu_prepare_xfer(eachDpu, dat + i * nrWordDpu);
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                           nrWordDpu * sizeof(uint32_t), DPU_XFER_DEFAULT));
  // Verify results
  for (size_t i = 0; i < nrWord; ++i) {
    if (dat[i] != expected[i])
      exit(fprintf(stderr, "Error at %zu: expt %u got %u\n", i, expected[i], dat[i]));
  }

  dpu_free(set);
  free(dat);
  free(expected);
  return 0;
}
