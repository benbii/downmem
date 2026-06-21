
#include <dpu.h>
#include <assert.h>
#include <stdlib.h>

#define ALIGN_UP(n, m) ((((n) + (m) - 1) / (m)) * (m))
#define DIV_CEIL(n, m) (((n) + (m) - 1) / (m))

int main(int argc, char **argv) {
  struct dpu_set_t dpu_set, dpu;
  int num_dpus = atoi(argv[1]), input_size = atoi(argv[2]);
  int size_per_dpu = DIV_CEIL(input_size, num_dpus);
  int dpu_aligned_size = ALIGN_UP(size_per_dpu, 8);
  int total_bytes = dpu_aligned_size * num_dpus * sizeof(int), transfer_bytes = dpu_aligned_size * sizeof(int);
  int *A = malloc(total_bytes), *B = malloc(total_bytes), *C = malloc(total_bytes), *C2 = malloc(total_bytes);
  int *bufferA = A, *bufferB = B, *bufferC = C2, i = 0;
  int transfer_size = transfer_bytes;

  DPU_ASSERT(dpu_alloc(num_dpus, NULL, &dpu_set));
  DPU_ASSERT(dpu_load(dpu_set, argv[3], NULL));

  for (int i = 0; i < input_size; i++) {A[i] = i; B[i] = i; C[i] = A[i] + B[i];}

// Push A at 0
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &bufferA[dpu_aligned_size * i]);}
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, transfer_bytes, DPU_XFER_DEFAULT));

  // Push B at 16384 
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &bufferB[dpu_aligned_size * i]); }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 16384, transfer_bytes, DPU_XFER_DEFAULT)); 

  DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));

  // Pull C from 32768 
  i = 0; DPU_FOREACH(dpu_set, dpu, i) {dpu_prepare_xfer(dpu, &bufferC[dpu_aligned_size * i]);}
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, 32768, transfer_bytes, DPU_XFER_DEFAULT));

  int errors = 0;
  for (i = 0; i < input_size; i++) {
    if (C[i] != bufferC[i]) {
        fprintf(stderr, "idx:%d host:%d dpu:%d\n", i, C[i], bufferC[i]);
        errors++;
    }
  }

  free(A); free(B); free(C); free(C2);
  DPU_ASSERT(dpu_free(dpu_set));  
  return 0;  
}  


/*#include <dpu.h>
#include <assert.h>
#include <stdlib.h>

#define ALIGN_UP(n, m) ((((n) + (m) - 1) / (m)) * (m))
#define DIV_CEIL(n, m) (((n) + (m) - 1) / (m))

int main(int argc, char **argv) {
  struct dpu_set_t dpu_set, dpu;
  int num_dpus = atoi(argv[2]), input_size = atoi(argv[1]);
  int size_per_dpu = DIV_CEIL(input_size, num_dpus);
  int dpu_aligned_size = ALIGN_UP(size_per_dpu, 8);
  int total_bytes = dpu_aligned_size * num_dpus * sizeof(int), transfer_bytes = dpu_aligned_size * sizeof(int);
  int *A = malloc(total_bytes), *B = malloc(total_bytes), *C = malloc(total_bytes), *C2 = malloc(total_bytes);
  int *bufferA = A, *bufferB = B, *bufferC = C2, i = 0;
  int transfer_size = transfer_bytes;

  DPU_ASSERT(dpu_alloc(num_dpus, NULL, &dpu_set));
  DPU_ASSERT(dpu_load(dpu_set, argv[3], NULL));

  for (int i = 0; i < input_size; i++) {A[i] = i; B[i] = i; C[i] = A[i] + B[i];}

  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &transfer_size); }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "TRANSFER_SIZE", 0, sizeof(uint32_t), DPU_XFER_DEFAULT));
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &bufferA[dpu_aligned_size * i]);}
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, transfer_bytes, DPU_XFER_DEFAULT));
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, &bufferB[dpu_aligned_size * i]); }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, transfer_bytes, transfer_bytes, DPU_XFER_DEFAULT)); 
  DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));
  i = 0; DPU_FOREACH(dpu_set, dpu, i) {dpu_prepare_xfer(dpu, &bufferC[dpu_aligned_size * i]);}
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, transfer_bytes, transfer_bytes, DPU_XFER_DEFAULT));

  for (i = 0; i < input_size; i++) 
    if (C[i] != bufferC[i]) return fprintf(stderr, "idx:%d h:%d d:%d\n", i, C[i], bufferC[i]);

  free(A); free(B); free(C); free(C2);
  DPU_ASSERT(dpu_free(dpu_set));
  return 0; 
} */
