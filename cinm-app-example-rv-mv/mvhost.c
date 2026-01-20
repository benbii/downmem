
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
  int *A = malloc(total_bytes), *B = malloc(total_bytes), *C = malloc(num_dpus * sizeof(int)), *C2 = malloc(num_dpus * 2 * sizeof(int));
  int *bufferA = A, *bufferB = B, *bufferC = C2, i = 0;

  DPU_ASSERT(dpu_alloc(num_dpus, NULL, &dpu_set));
  DPU_ASSERT(dpu_load(dpu_set, argv[3], NULL));

  // Initialize matrix A (each row per DPU) and vector B
  // For mv_dimm8_nopt: each DPU reads 512 elements
  for (int i = 0; i < num_dpus; i++) {
    for (int j = 0; j < dpu_aligned_size; j++) {
      if (j < vector_size) {
        A[i * dpu_aligned_size + j] = i * vector_size + j;  // Matrix row i
      } else {
        A[i * dpu_aligned_size + j] = 0;  // Padding
      }
    }
  }
  
  for (int i = 0; i < vector_size; i++) {
    B[i] = i;
  }
  
  // Compute expected results: C[i] = sum(A[i][j] * B[j]) for row i
  for (int i = 0; i < num_dpus; i++) {
    C[i] = 0;
    for (int j = 0; j < vector_size; j++) {
      C[i] += A[i * dpu_aligned_size + j] * B[j];
    }
  }

  // Push matrix rows A at offset 0 (each DPU gets its row at offset i*2048)
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &bufferA[dpu_aligned_size * i]);}
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, transfer_bytes, DPU_XFER_DEFAULT));

  // Push vector B at offset 2048 (each DPU gets same vector portion at offset 2048 + i*2048)
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &bufferB[dpu_aligned_size * i]); }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 2048, transfer_bytes, DPU_XFER_DEFAULT)); 

  DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));

  // Pull results C from offset 4096 (each DPU writes at offset 4096 + i*8)
  i = 0; DPU_FOREACH(dpu_set, dpu, i) {dpu_prepare_xfer(dpu, &bufferC[i * 2]);}
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, 4096, num_dpus * 2 * sizeof(int), DPU_XFER_DEFAULT));

  int errors = 0;
  for (i = 0; i < num_dpus; i++) {
    if (C[i] != bufferC[i * 2]) {
        fprintf(stderr, "idx:%d host:%d dpu:%d\n", i, C[i], bufferC[i * 2]);
        errors++;
    }
  }

  if (errors == 0) {
    printf("Matrix-vector multiplication completed successfully!\n");
  } else {
    printf("Found %d errors\n", errors);
  }

  free(A); free(B); free(C); free(C2);
  DPU_ASSERT(dpu_free(dpu_set));  
  return errors;  
}  

