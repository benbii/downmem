#include "alloc.h"
#include "syslib.h"

// Simple dynamic memory allocator using CSR0 bit 31 for synchronization
static uint8_t *heap_ptr = &__sys_used_scratchpad_end;

void *mem_alloc(size_t size) {
  uint32_t lock_mask = 1u << 31;
  uint32_t prev;
  // Atomic test-and-set on CSR0 bit 31 for heap synchronization
  do {
    __asm__ volatile("csrrs %0, 0x000, %1" : "=r"(prev) : "r"(lock_mask));
  } while (prev & lock_mask);
  // Allocate memory
  void *result = heap_ptr;
  heap_ptr += size;
  // Release lock
  __asm__ volatile("csrrc zero, 0x000, %0" : : "r"(lock_mask));
  return result;
}

void mem_reset(void) {
  // No locking needed - apps protect this with barriers
  heap_ptr = &__sys_used_scratchpad_end;
}
