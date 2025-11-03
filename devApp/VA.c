#include <alloc.h>
#include <defs.h>
#include <mram.h>

__host uint32_t TRANSFER_SIZE;

#define T int32_t
#define SHIFT_DIV 2 // Shift right to divide by sizeof(T)
#define BLOCK_SIZE_LOG2 8
#define BLOCK_SIZE (1 << BLOCK_SIZE_LOG2)

void __attribute__((noinline))
vector_addition(T *bufferB, T *bufferA, unsigned int l_size) {
  for (unsigned int i = 0; i < l_size; i++) bufferB[i] += bufferA[i];
}

int main() {
  unsigned int tasklet_id = me();
  uint32_t transfer_size = TRANSFER_SIZE; // Transfer input size per DPU in bytes
  uint32_t total_size = transfer_size; // Input size per DPU in bytes
  uint32_t base_tasklet = tasklet_id << BLOCK_SIZE_LOG2;
  uintptr_t mram_base_addr_A = (uintptr_t)DPU_MRAM_HEAP_POINTER;
  uintptr_t mram_base_addr_B = (uintptr_t)(DPU_MRAM_HEAP_POINTER + transfer_size);
  T cache_A[BLOCK_SIZE >> SHIFT_DIV], cache_B[BLOCK_SIZE >> SHIFT_DIV];

  for (unsigned int byte_index = base_tasklet; byte_index < total_size; byte_index += BLOCK_SIZE * NR_TASKLETS) {
    uint32_t l_size_bytes = (byte_index + BLOCK_SIZE >= total_size) ? (total_size - byte_index) : BLOCK_SIZE;
    mram_read((__mram_ptr void const *)(mram_base_addr_A + byte_index), cache_A, l_size_bytes);
    mram_read((__mram_ptr void const *)(mram_base_addr_B + byte_index), cache_B, l_size_bytes);
    vector_addition(cache_B, cache_A, l_size_bytes >> SHIFT_DIV);
    mram_write(cache_B, (__mram_ptr void *)(mram_base_addr_B + byte_index), l_size_bytes);
  }
  return 0;
}
