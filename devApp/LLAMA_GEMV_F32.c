#include "moredefs.h"
#include <alloc.h>
#include <stdint.h>

typedef struct {
  uint32_t k;
  uint32_t k_pad;
  uint32_t n_rows;
  uint32_t max_rows;
  uint32_t a_offset;
  uint32_t x_offset;
  uint32_t y_offset;
} llama_gemv_f32_args_t;

__host llama_gemv_f32_args_t DPU_INPUT_ARGUMENTS;

ALL_THREADS_BARRIER_INIT();

int main(void) {
  all_threads_barrier_wait();
  return 0;
}
