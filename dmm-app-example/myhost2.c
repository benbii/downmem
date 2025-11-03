#include <assert.h>
#include <dpu.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  size_t nrDpu = atoi(argv[2]), nrHst = atoi(argv[1]), i;
  struct dpu_set_t set, eachDpu;
  DPU_ASSERT(dpu_alloc(nrDpu, NULL, &set));
  DPU_ASSERT(dpu_load(set, argv[3], NULL));

  int a[nrDpu];
  DPU_FOREACH(set, eachDpu, i) {
    a[i] = i;
    // wram min 4B; mram min 8B
    dpu_prepare_xfer(eachDpu, &a[i]);
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "a", 0, 4, DPU_XFER_NO_RESET));

  DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, "a", 0, 4, DPU_XFER_DEFAULT));
  for (i = 0; i < nrDpu; ++i)
    if (a[i] != i * 2)
      return fprintf(stderr, "BAD at %zu: expt %zu got %d\n", i, i * 2, a[i]);
  return 0;
}
