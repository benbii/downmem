/*
 * Vector addition with multiple tasklets
 *
 */
#include <alloc.h>
#include <defs.h>
#include <mram.h>

typedef struct {
  uint32_t size;
  uint32_t transfer_size;
  enum kernels {
    kernel1 = 0,
    nr_kernels = 1,
  } kernel;
} dpu_arguments_t;
#define T int32_t
#define DIV 2 // Shift right to divide by sizeof(T)
#define BLOCK_SIZE_LOG2 8
#define BLOCK_SIZE (1 << BLOCK_SIZE_LOG2)
__host dpu_arguments_t DPU_INPUT_ARGUMENTS;

// vector_addition: Computes the vector addition of a cached block
void __attribute__((noinline))
vector_addition(T *bufferB, T *bufferA, unsigned int l_size) {
  for (unsigned int i = 0; i < l_size; i++) {
    bufferB[i] += bufferA[i];
  }
}

// Barrier
extern int main_kernel1(void);
int (*kernels[nr_kernels])(void) = {main_kernel1};

int main(void) {
  // Kernel
  return kernels[DPU_INPUT_ARGUMENTS.kernel]();
}

// main_kernel1
int main_kernel1() {
  unsigned int tasklet_id = me();
#if PRINT
  printf("tasklet_id = %u\n", tasklet_id);
#endif
  uint32_t input_size_dpu_bytes =
      DPU_INPUT_ARGUMENTS.size; // Input size per DPU in bytes
  uint32_t input_size_dpu_bytes_transfer =
      DPU_INPUT_ARGUMENTS.transfer_size; // Transfer input size per DPU in bytes

  // Address of the current processing block in MRAM
  uint32_t base_tasklet = tasklet_id << BLOCK_SIZE_LOG2;
  uintptr_t mram_base_addr_A = (uintptr_t)DPU_MRAM_HEAP_POINTER;
  uintptr_t mram_base_addr_B =
      (uintptr_t)(DPU_MRAM_HEAP_POINTER + input_size_dpu_bytes_transfer);

  // Initialize a local cache to store the MRAM block
  // T *cache_A = (T *)mem_alloc(BLOCK_SIZE);
  // T *cache_B = (T *)mem_alloc(BLOCK_SIZE);
  T cache_A[BLOCK_SIZE >> DIV], cache_B[BLOCK_SIZE >> DIV];

  for (unsigned int byte_index = base_tasklet;
       byte_index < input_size_dpu_bytes;
       byte_index += BLOCK_SIZE * NR_TASKLETS) {

    // Bound checking
    uint32_t l_size_bytes = (byte_index + BLOCK_SIZE >= input_size_dpu_bytes)
                                ? (input_size_dpu_bytes - byte_index)
                                : BLOCK_SIZE;
    // const uint32_t l_size_bytes = BLOCK_SIZE;

    // Load cache with current MRAM block
    mram_read((__mram_ptr void const *)(mram_base_addr_A + byte_index), cache_A,
              l_size_bytes);
    mram_read((__mram_ptr void const *)(mram_base_addr_B + byte_index), cache_B,
              l_size_bytes);

    // Computer vector addition
    vector_addition(cache_B, cache_A, l_size_bytes >> DIV);

    // Write cache to current MRAM block
    mram_write(cache_B, (__mram_ptr void *)(mram_base_addr_B + byte_index),
               l_size_bytes);
  }

  return 0;
}
