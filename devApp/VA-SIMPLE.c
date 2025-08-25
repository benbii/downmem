#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <stddef.h>
#include <stdint.h>

__host uint32_t nrWords;

int main() {
  uint8_t bufW_[512];
  size_t tlNrWords = (nrWords + NR_TASKLETS - 1) / NR_TASKLETS;
  size_t tlNrBytes = sizeof(uint32_t) * tlNrWords;
  
  // Cast to fix pointer type warnings
  __mram_ptr uint32_t *opM = (__mram_ptr uint32_t *)(DPU_MRAM_HEAP_POINTER + me() * tlNrBytes);
  __mram_ptr uint32_t *resM = opM + NR_TASKLETS * tlNrWords;
  uint32_t *bufW = (uint32_t *)bufW_;

  size_t i = 0;
  // Process in chunks of 128 uint32_t (512 bytes)
  for (; i + 128 < tlNrWords; i += 128) {
    mram_read(opM + i, bufW, 512);
    for (size_t j = 0; j < 128; j += 2) {
      // Simple 32-bit addition instead of complex demo32()
      bufW[j] = bufW[j] + bufW[j + 1];     // a + b
      bufW[j + 1] = bufW[j] + bufW[j + 1]; // (a + b) + b = a + 2b
    }
    mram_write(bufW, resM + i, 512);
  }

  // Handle remaining elements
  size_t remaining = tlNrWords - i;
  if (remaining > 0) {
    mram_read(opM + i, bufW, remaining * sizeof(uint32_t));
    for (size_t j = 0; j < remaining && j + 1 < remaining; j += 2) {
      // Simple 32-bit addition
      bufW[j] = bufW[j] + bufW[j + 1];     // a + b
      bufW[j + 1] = bufW[j] + bufW[j + 1]; // (a + b) + b = a + 2b
    }
    mram_write(bufW, resM + i, remaining * sizeof(uint32_t));
  }
  
  return 0;
}