
#include <dpu.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#define ALIGN_UP(n, m) ((((n) + (m) - 1) / (m)) * (m))
#define DIV_CEIL(n, m) (((n) + (m) - 1) / (m))

int main(int argc, char **argv) {
  struct dpu_set_t dpu_set, dpu;
  int num_dpus = atoi(argv[1]), vector_size = atoi(argv[2]);
  int size_per_dpu = DIV_CEIL(vector_size, num_dpus);
  int dpu_aligned_size = ALIGN_UP(size_per_dpu, 8);
  int total_bytes = dpu_aligned_size * num_dpus * sizeof(int), transfer_bytes = dpu_aligned_size * sizeof(int);
  // For mm_dimm1_nopt: each DPU processes 1024 elements (512x2 for matrix row and vector)
  // Matrix A: each DPU gets a row at offset DPU_ID * 4096
  // Vector B: all DPUs get the same vector at offset 4096 + DPU_ID * 4096
  // Result C: each DPU writes result at offset 8192 + DPU_ID * 8
  int *A = malloc(num_dpus * 1024 * sizeof(int)), *B = malloc(1024 * sizeof(int));
  int *C = malloc(num_dpus * sizeof(int)), *C2 = malloc(num_dpus * 2 * sizeof(int));
  int *bufferA = A, *bufferB = B, *bufferC = C2, i = 0;

  DPU_ASSERT(dpu_alloc(num_dpus, NULL, &dpu_set));
  DPU_ASSERT(dpu_load(dpu_set, argv[3], NULL));

  // Initialize matrix A (each row per DPU) and vector B
  // For mm_dimm1_nopt: each DPU reads 1024 elements (2 * 512)
  for (int i = 0; i < num_dpus; i++) {
    for (int j = 0; j < 1024; j++) {
      A[i * 1024 + j] = i * 1024 + j;  // Matrix row i
    }
  }
  
  for (int i = 0; i < 1024; i++) {
    B[i] = i;
  }
  
  // Compute expected results: C[i] = sum(A[i][j] * B[j]) for row i
  for (int i = 0; i < num_dpus; i++) {
    C[i] = 0;
    for (int j = 0; j < 1024; j++) {
      C[i] += A[i * 1024 + j] * B[j];
    }
  }

  // Push matrix rows A at offset 0 (each DPU gets its row at offset i*4096)
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &bufferA[1024 * i]);}
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, 1024 * sizeof(int), DPU_XFER_DEFAULT));

  // Push vector B at offset 4096 (each DPU gets same vector at offset 4096 + i*4096)
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &bufferB[0]); }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 4096, 1024 * sizeof(int), DPU_XFER_DEFAULT)); 

  DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));

  // Pull results C from offset 8192 (each DPU writes at offset 8192 + i*8)
  i = 0; DPU_FOREACH(dpu_set, dpu, i) {dpu_prepare_xfer(dpu, &bufferC[i * 2]);}
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, 8192, num_dpus * 2 * sizeof(int), DPU_XFER_DEFAULT));

  int errors = 0;
  for (i = 0; i < num_dpus; i++) {
    if (C[i] != bufferC[i * 2]) {
        fprintf(stderr, "idx:%d host:%d dpu:%d\n", i, C[i], bufferC[i * 2]);
        errors++;
    }
  }

  if (errors == 0) {
    printf("Matrix-vector multiplication (MLP) completed successfully!\n");
  } else {
    printf("Found %d errors\n", errors);
  }

  free(A); free(B); free(C); free(C2);
  DPU_ASSERT(dpu_free(dpu_set));  
  return errors;  
}  

