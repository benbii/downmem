#include <assert.h>
#include <dpu.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  uint32_t nrWord = atoi(argv[1]), nrDpu = atoi(argv[2]);
  nrWord = ((nrWord + nrDpu - 1) / nrDpu) * nrDpu;
  uint32_t nrWordDpu = nrWord / nrDpu;
  uint32_t *dat = malloc(nrWord * sizeof(uint32_t));
  uint64_t res, expected = 0;
  for (size_t i = 0; i < nrWord; ++i) {
    dat[i] = i % 71;
    expected += dat[i];
  }

  struct dpu_set_t set, eachDpu;
  DPU_ASSERT(dpu_alloc(nrDpu, NULL, &set));
  DPU_ASSERT(dpu_load(set, argv[3], NULL));
  DPU_ASSERT(dpu_broadcast_to(set, "nrWord", 0, &nrWordDpu, sizeof(uint32_t),
                              DPU_XFER_DEFAULT));

  size_t i;
  DPU_FOREACH(set, eachDpu, i) {
    dpu_prepare_xfer(eachDpu, dat + i * nrWordDpu);
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                           nrWordDpu * sizeof(uint32_t), DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

  uint32_t total = 0, *results = malloc(nrDpu * sizeof(uint32_t));
  DPU_FOREACH(set, eachDpu, i) { dpu_prepare_xfer(eachDpu, &results[i]); }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, "dpu_glob_result", 0,
                           sizeof(uint32_t), DPU_XFER_DEFAULT));
  DPU_FOREACH(set, eachDpu, i)
  total += results[i];

  assert(total == expected);
  dpu_free(set);
  free(dat);
  free(results);
  return total != expected;
}
