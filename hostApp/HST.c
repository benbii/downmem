#include <assert.h>
#include <dpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_BINS 16
#define BINS_PER_MUTEX 4

int main(int argc, char **argv) {
  uint32_t num_elements = atoi(argv[1]);
  const char *dpu_binary = argv[3];
  uint32_t num_dpus = atoi(argv[2]);
  num_elements = (num_elements + num_dpus - 1) / num_dpus * num_dpus;
  uint32_t elements_per_dpu = num_elements / num_dpus;

  // Initialize DPU system
  struct dpu_set_t set, each;
  size_t idx_dpu;
  DPU_ASSERT(dpu_alloc(num_dpus, NULL, &set));
  DPU_ASSERT(dpu_load(set, dpu_binary, NULL));

  // Generate random data
  uint32_t *input_data = malloc(num_elements * sizeof(uint32_t));
  assert(input_data != NULL);
  srand(time(NULL));
  for (uint32_t i = 0; i < num_elements; i++)
    input_data[i] = rand() % (NUM_BINS * 65536);
  // Compute host-side histogram for verification
  uint32_t host_bins[NUM_BINS] = {0};
  for (uint32_t i = 0; i < num_elements; i++) {
    uint32_t bin = input_data[i] / 65536;
    if (bin >= NUM_BINS)
      bin = NUM_BINS - 1; // Handle edge case
    host_bins[bin]++;
  }

  // Distribute data across DPUs (simplified - each DPU gets same amount)
  uint32_t offset = 0;
  DPU_FOREACH(set, each, idx_dpu) {
    dpu_prepare_xfer(each, input_data + offset);
    offset += elements_per_dpu;
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "__sys_used_mram_end", 0,
                           elements_per_dpu * sizeof(uint32_t),
                           DPU_XFER_DEFAULT));
  // Broadcast elements_per_dpu to all DPUs
  DPU_ASSERT(dpu_broadcast_to(set, "num_elements", 0, &elements_per_dpu,
                              sizeof(uint32_t), DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));
  // Collect results from DPUs
  uint32_t dpu_bins[NUM_BINS] = {0};

  uint32_t result_bins[num_dpus][NUM_BINS];
  DPU_FOREACH(set, each, idx_dpu) {
    dpu_prepare_xfer(each, result_bins[idx_dpu]);
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_FROM_DPU, "global_bins", 0,
                           NUM_BINS * sizeof(uint32_t), DPU_XFER_DEFAULT));
  // Accumulate results
  DPU_FOREACH(set, each, idx_dpu) {
    for (int i = 0; i < NUM_BINS; i++)
      dpu_bins[i] += result_bins[idx_dpu][i];
  }

  // Verify results
  for (int i = 0; i < NUM_BINS; i++)
    if (host_bins[i] != dpu_bins[i])
      return fprintf(stderr, "%2d\t%u\t%u ✗\n", i, host_bins[i], dpu_bins[i]);
  dpu_free(set);
  return 0;
}
