#include <assert.h>
#include <dpu.h>
#include <stdlib.h>

#define NUM_LAYERS 3
#define T int32_t
typedef struct { uint32_t n_size, n_size_pad, nr_rows, max_rows; } dpu_args_t;
struct dpu_info_t { uint32_t rows_per_dpu, rows_pad, prev_rows_dpu; };
bool status = true; static T **A, *B, *B_tmp, *C, *C_dpu; 

int main(int argc, char **argv) {
  struct dpu_set_t dpu_set, dpu;
  uint32_t nr_of_dpus;
  unsigned int m_size = atoi(argv[1]), n_size = m_size;

  DPU_ASSERT(dpu_alloc(atoi(argv[2]), NULL, &dpu_set));
  DPU_ASSERT(dpu_load(dpu_set, argv[3], NULL));
  DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &nr_of_dpus));

  struct dpu_info_t *dpu_info = malloc(nr_of_dpus * sizeof(struct dpu_info_t));
  dpu_args_t *input_args = malloc(nr_of_dpus * sizeof(dpu_args_t));
  uint32_t max_rows = 0, n_size_pad = (n_size % 2) ? n_size + 1 : n_size;
  uint32_t n_pad_bytes = n_size_pad * sizeof(T);

  unsigned int i = 0; DPU_FOREACH(dpu_set, dpu, i) {
    uint32_t prev = 0;
    uint32_t chunks = m_size / nr_of_dpus, rest = m_size % nr_of_dpus; 
    uint32_t rows = chunks + (i < rest ? 1 : 0);
    if (rest > 0) {
      if (i >= rest) prev = rest * (chunks + 1) + (i - rest) * chunks;
      else prev = i * (chunks + 1);
    } else prev = i * chunks;

    // Keep max rows for parallel transfers
    uint32_t rows_pad = (rows % 2) ? rows + 1 : rows;
    if (rows_pad > max_rows) max_rows = rows_pad;
    dpu_info[i] = (struct dpu_info_t){rows, rows_pad, prev};
    input_args[i] = (dpu_args_t){n_size, n_size_pad, rows};
  }

  uint32_t max_rows_bytes = max_rows * sizeof(T);
  A = malloc(NUM_LAYERS * sizeof(T *));
  for (unsigned int l = 0; l < NUM_LAYERS; l++)
    A[l] = malloc(max_rows * nr_of_dpus * n_pad_bytes);

  B = malloc(n_size * sizeof(T)); C = malloc(m_size * sizeof(T)); 
  C_dpu = malloc(max_rows * nr_of_dpus * sizeof(T));
  B_tmp = malloc(max_rows * nr_of_dpus * sizeof(T));

  // Init data  
  for (unsigned int l = 0; l < NUM_LAYERS; l++)
  for (unsigned int i = 0; i < m_size * n_size; i++) 
    A[l][i] = (i % 100 < 98) ? 0 : (l + i) % 2;
  for (unsigned int i = 0; i < n_size; i++) 
    B[i] = (i % 50 < 48) ? 0 : i % 2;

  // Mlp host
  for (unsigned int nl = 0; nl < NUM_LAYERS; nl++) {
    for (unsigned int m = 0; m < m_size; m++) C[m] = 0;
    for (unsigned int m = 0; m < m_size; m++) {
      for (unsigned int n = 0; n < n_size; n++) 
        C[m] += A[nl][m * n_size + n] * B[n];
      C[m] = (C[m] > 0) ? C[m] : 0;
    }
    for (unsigned int n = 0; n < n_size; n++) B[n] = C[n];
  }

  // Copy input array and vector
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { input_args[i].max_rows = max_rows; dpu_prepare_xfer(dpu, input_args + i); }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0, sizeof(dpu_args_t), DPU_XFER_DEFAULT));
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, A[0] + dpu_info[i].prev_rows_dpu * n_size); }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, max_rows * n_pad_bytes, DPU_XFER_DEFAULT));
  i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, B); }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 
    max_rows * n_pad_bytes, n_pad_bytes, DPU_XFER_DEFAULT));
  DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));

  for (int lay = 1, i = 0; lay < NUM_LAYERS; lay++) {
    // Copy C_dpu
    DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, C_dpu + i * max_rows); }
    DPU_ASSERT(dpu_push_xfer( dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, 
      max_rows * n_pad_bytes + n_pad_bytes, max_rows_bytes, DPU_XFER_DEFAULT));

    // B = C
    for (unsigned int n = 0, i = 0; n < nr_of_dpus; n++) 
      for (unsigned int j = 0; j < dpu_info[n].rows_per_dpu; j++) 
        B_tmp[i++] = C_dpu[n * max_rows + j];
    i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, B_tmp); }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 
      max_rows * n_pad_bytes, n_pad_bytes, DPU_XFER_DEFAULT));

    // Copy next matrix of weights
    i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, A[lay] + dpu_info[i].prev_rows_dpu * n_size);}
    DPU_ASSERT(dpu_push_xfer( dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, max_rows * n_pad_bytes, DPU_XFER_DEFAULT));
    DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));

    // Retrieve results
    i = 0; DPU_FOREACH(dpu_set, dpu, i) { dpu_prepare_xfer(dpu, C_dpu + i * max_rows); }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, 
      max_rows * n_pad_bytes + n_pad_bytes, max_rows_bytes, DPU_XFER_DEFAULT));
  }

  for (unsigned int n = 0, i = 0; n < nr_of_dpus; n++) {
    for (unsigned int j = 0; j < dpu_info[n].rows_per_dpu; j++, i++) 
      if (C[i] != C_dpu[n * max_rows + j]) status = false;
  }

  for (i = 0; i < NUM_LAYERS; i++) free(A[i]);
  free(A); free(B); free(C); free(C_dpu); free(dpu_info); free(input_args); free(B_tmp);
  DPU_ASSERT(dpu_free(dpu_set));
  return status ? 0 : -1;
}
