#include <dpu.h>
#include <assert.h>
#include <stdlib.h>

// For simplicity we give all DPU identical data. For  cases consider the
// last element of each DPU.
int main(int argc, char **argv) {
  uint32_t nrWord = atoi(argv[1]), nrDpu = atoi(argv[2]);
  uint32_t *dat = malloc(nrWord * sizeof(uint32_t)), res;
  for (size_t i = 0; i < nrWord; ++i)
    dat[i] = i / 5;

  struct dpu_set_t set, eachDpu;
  DPU_ASSERT(dpu_alloc(nrDpu, NULL, &set));
  DPU_ASSERT(dpu_load(set, argv[3], NULL));

  DPU_ASSERT(dpu_broadcast_to(set, "nrWord", 0, &nrWord, sizeof(uint32_t),
                              DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_broadcast_to(set, DPU_MRAM_HEAP_POINTER_NAME, 0, dat,
                              nrWord * sizeof(uint32_t), DPU_XFER_DEFAULT));

  DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));
  DPU_FOREACH(set, eachDpu) { dpu_prepare_xfer(eachDpu, &res); }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, "res", 0, sizeof(uint32_t),
                           DPU_XFER_DEFAULT));

  assert(dat[nrWord - 1] == res);
  dpu_free(set);
  return dat[nrWord - 1] != res;
}
