#include <dpu.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

// Simple 32-bit addition function for CPU reference
static uint32_t simple_add(uint32_t a, uint32_t b) {
  return a + b;
}

// TODO: dynamically determine NR_TASKLETS
#ifndef NR_TASKLETS
#define NR_TASKLETS 16
#endif

// Usage: ./va-simple <total_words> <nr_dpus> <binary_path>
int main(int argc, char** argv) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <total_words> <nr_dpus> <binary_path>\n", argv[0]);
    return 1;
  }
  
  struct dpu_set_t dpuSet, dpu;
  size_t totWords = atoi(argv[1]);
  size_t nrDpus = atoi(argv[2]);
  size_t i;
  
  // Ensure words are divisible by 2 * NR_TASKLETS * nrDpus for proper distribution
  if (totWords % (2 * NR_TASKLETS * nrDpus) != 0) {
    totWords = ((totWords + 2 * NR_TASKLETS * nrDpus - 1) / (2 * NR_TASKLETS * nrDpus)) * 2 * NR_TASKLETS * nrDpus;
    printf("Adjusting total words to %zu for proper distribution\n", totWords);
  }
  
  const size_t nrWords = totWords / nrDpus;
  printf("Testing VA-SIMPLE: %zu total words, %zu DPUs, %zu words per DPU\n", 
         totWords, nrDpus, nrWords);

  DPU_ASSERT(dpu_alloc(nrDpus, NULL, &dpuSet));
  DPU_ASSERT(dpu_load(dpuSet, argv[3], NULL));

  // Allocate memory for input and results
  uint32_t* inputs = malloc(totWords * sizeof(uint32_t));
  uint32_t* hostRes = malloc(totWords * sizeof(uint32_t));
  uint32_t* devRes = malloc(totWords * sizeof(uint32_t));
  
  if (!inputs || !hostRes || !devRes) {
    fprintf(stderr, "Memory allocation failed\n");
    return 1;
  }

  // Initialize test data with simple patterns
  srand(12345);
  for (size_t i = 0; i < totWords; ++i) {
    inputs[i] = rand() % 1000; // Keep numbers small for easy verification
  }

  // Compute reference results on CPU
  for (size_t i = 0; i < totWords; i += 2) {
    if (i + 1 < totWords) {
      uint32_t a = inputs[i];
      uint32_t b = inputs[i + 1];
      hostRes[i] = a + b;           // a + b
      hostRes[i + 1] = (a + b) + b; // (a + b) + b = a + 2b
    } else {
      hostRes[i] = inputs[i]; // Handle odd number case
    }
  }

  // Initialize device result buffer
  memset(devRes, 0x42, totWords * sizeof(uint32_t));

  // Send parameters to DPUs
  dpu_broadcast_to(dpuSet, "nrWords", 0, &nrWords, sizeof(nrWords), DPU_XFER_DEFAULT);

  // Send input data to DPUs
  DPU_FOREACH(dpuSet, dpu, i) {
    dpu_prepare_xfer(dpu, inputs + nrWords * i);
  }
  dpu_push_xfer(dpuSet, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                nrWords * sizeof(uint32_t), DPU_XFER_DEFAULT);

  printf("Launching DPU computation...\n");
  dpu_launch(dpuSet, DPU_SYNCHRONOUS);

  // Retrieve results from DPUs
  DPU_FOREACH(dpuSet, dpu, i) {
    dpu_prepare_xfer(dpu, devRes + nrWords * i);
  }
  dpu_push_xfer(dpuSet, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                nrWords * sizeof(uint32_t), nrWords * sizeof(uint32_t),
                DPU_XFER_DEFAULT);

  // Verify results
  printf("Verifying results...\n");
  size_t errors = 0;
  for (size_t i = 0; i < totWords && errors < 10; ++i) {
    if (devRes[i] != hostRes[i]) {
      printf("ERROR at index %zu: expected %u, got %u\n", i, hostRes[i], devRes[i]);
      errors++;
    }
  }

  if (errors == 0) {
    printf("SUCCESS: All %zu results match!\n", totWords);
  } else {
    printf("FAILED: %zu errors found\n", errors);
    free(inputs);
    free(hostRes);
    free(devRes);
    dpu_free(dpuSet);
    return 1;
  }

  free(inputs);
  free(hostRes);
  free(devRes);
  dpu_free(dpuSet);
  return 0;
}