#include <assert.h>
#include <dpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv) {
  uint32_t num_elements = atoi(argv[1]);
  uint32_t num_dpus = atoi(argv[2]);
  const char *dpu_binary = argv[3];
  num_elements = (num_elements + num_dpus - 1) / num_dpus * num_dpus;
  uint32_t elements_per_dpu = num_elements / num_dpus;
  // Initialize DPU system
  struct dpu_set_t set, dpu;
  uint32_t each_dpu;
  DPU_ASSERT(dpu_alloc(num_dpus, NULL, &set));
  DPU_ASSERT(dpu_load(set, dpu_binary, NULL));

  // Generate random input data
  uint32_t *input_data = malloc(num_elements * sizeof(uint32_t));
  assert(input_data != NULL);
  srand(time(NULL));
  uint32_t expected_odds = 0;
  for (uint32_t i = 0; i < num_elements; i++) {
    // input_data[i] = i * 2;
    input_data[i] = rand();
    if (input_data[i] % 2 == 1)
      expected_odds++;
  }
  // Compute host reference result
  uint32_t *host_result = malloc(expected_odds * sizeof(uint32_t));
  assert(host_result != NULL);
  uint32_t host_count = 0;
  for (uint32_t i = 0; i < num_elements; i++)
    if (input_data[i] % 2 == 1)
      host_result[host_count++] = input_data[i];

  // Distribute data across DPUs (8-byte aligned)
  uint32_t offset = 0;
  DPU_FOREACH(set, dpu, each_dpu) {
    dpu_prepare_xfer(dpu, input_data + offset);
    offset += elements_per_dpu;
  }
  DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "__sys_used_mram_end", 0,
                           elements_per_dpu * sizeof(uint32_t),
                           DPU_XFER_DEFAULT));
  // Broadcast elements_per_dpu to all DPUs
  DPU_ASSERT(dpu_broadcast_to(set, "num_elements", 0, &elements_per_dpu,
                              sizeof(uint32_t), DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

  // Get the total count of elements from each DPU
  uint32_t total_output = 0;
  uint32_t *dpu_counts = malloc(num_dpus * sizeof(uint32_t));
  assert(dpu_counts != NULL);
  DPU_FOREACH(set, dpu, each_dpu) {
    DPU_ASSERT(dpu_copy_from(dpu, "out_at", 0, &dpu_counts[each_dpu],
                             sizeof(uint32_t)));
    total_output += dpu_counts[each_dpu];
  }
  printf("TODO bulk xfer %u\n", total_output);
  // Collect all output elements from DPUs
  uint32_t *dpu_result = malloc(total_output * sizeof(uint32_t));
  assert(dpu_result != NULL);
  uint32_t result_offset = 0;
  DPU_FOREACH(set, dpu, each_dpu) {
    if (dpu_counts[each_dpu] > 0) {
      DPU_ASSERT(dpu_copy_from(dpu, "output_data", 0,
                               &dpu_result[result_offset],
                               dpu_counts[each_dpu] * sizeof(uint32_t)));
      result_offset += dpu_counts[each_dpu];
    }
  }

  // Count actual odd numbers (ignore padding zeros)
  size_t actual_odds = 0;
  for (uint32_t i = 0; i < total_output; i++) {
    if (dpu_result[i] == 0) {
      continue;
    } else if (dpu_result[i] % 2 == 0) {
      return fprintf(stderr, "ERROR - Even number found at index %u: %u\n", i,
                     dpu_result[i]);
    } else {
      actual_odds++; // Valid odd number
    }
  }
  // Check if we got the expected number of odd numbers
  if (actual_odds != expected_odds) {
    for (size_t i = 0; i < total_output; ++i)
      fprintf(stderr, "%u ", dpu_result[i]);
    return fprintf(stderr, "\nFAIL - Expected %u, got %zu odd numbers\n",
                   expected_odds, actual_odds);
  } 
  // Cleanup
  free(input_data); free(host_result); free(dpu_result); free(dpu_counts);
  DPU_ASSERT(dpu_free(set));
  return 0;
}
