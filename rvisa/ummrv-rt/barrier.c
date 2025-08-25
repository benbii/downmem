#include "barrier.h"
#include "syslib.h"
#include <stdbool.h>

// CSR2 manipulation helpers for barrier metadata locking (shared with spinlock)
static inline uint32_t csr2_test_and_set(uint32_t mask) {
    uint32_t prev;
    __asm__ volatile ("csrrs %0, 0x002, %1" : "=r"(prev) : "r"(mask));
    return prev;
}

static inline void csr2_clear(uint32_t mask) {
    __asm__ volatile ("csrrc zero, 0x002, %0" : : "r"(mask));
}

// CSR3 manipulation for known thread barriers (dedicated CSR word)
static inline uint32_t csr3_test_and_set(uint32_t mask) {
    uint32_t prev;
    __asm__ volatile ("csrrs %0, 0x003, %1" : "=r"(prev) : "r"(mask));
    return prev;
}

void barrier_wait(barrier_t *barrier) {
  uint32_t msk = 1u << barrier->nthBitInCsr2;
  uint32_t id = me();
  uint32_t thread_bit = 1u << id;
  // Get exclusive access to barrier metadata
  while (csr2_test_and_set(msk) & msk) ;
  // Increment arrival counter
  barrier->current_count++;

  if (barrier->current_count == barrier->expected_count) {
    // Last thread - wake up all sleeping threads and reset barrier
    uint32_t wake_mask = barrier->sleeping_mask;
    uint32_t expected_mask =
        wake_mask & ~thread_bit; // All sleeping threads except current one
    barrier->current_count = 0;
    barrier->sleeping_mask = 0;
    // Release metadata lock first
    csr2_clear(msk);
    // Wake up all sleeping threads and check they were all actually sleeping
    uint32_t prev_running;
    __asm__ volatile("csrrs %0, 0x000, %1"
                     : "=r"(prev_running) : "r"(wake_mask));
    // Check if some threads haven't slept yet (were still running)
    uint32_t still_running = prev_running & expected_mask;
    // Keep waking threads that were still running when we first tried
    while (still_running) {
      // Small delay to let threads actually sleep
      __asm__ volatile("addi x0, x0, 0");
      // Try waking the stragglers again
      __asm__ volatile("csrrs %0, 0x000, %1"
                       : "=r"(prev_running) : "r"(still_running));
      still_running = prev_running & still_running;
    }

  } else {
    // Not the last thread - add self to sleeping set and sleep
    barrier->sleeping_mask |= thread_bit;
    // Release metadata lock
    csr2_clear(msk);
    // Go to sleep until woken by last thread
    halt(id);
  }
}

void known_barrier_wait(uint32_t thread_mask) {
  uint32_t id = me();
  uint32_t my_bit = 1u << id;
  // Atomically clear my bit and get previous value
  uint32_t prev = csr3_test_and_set(my_bit);

  // Check if we were the last thread (our bit was the only one not set)
  if (thread_mask == (prev | my_bit)) {
    // Reinitialize for next use
    __asm__ volatile("csrrwi x0, 0x003, 0");
    uint32_t to_wake = prev, wake_result;
    // Wake all sleeping threads
    __asm__ volatile("csrrs %0, 0x000, %1" : "=r"(wake_result) : "r"(to_wake));
    uint32_t still_running = wake_result & to_wake;
    // Check if some threads haven't slept yet
    while (still_running) {
      // Keep waking threads that were still running
      __asm__ volatile("csrrs %0, 0x000, %1" : "=r"(wake_result) : "r"(still_running));
      still_running = wake_result & still_running;
    }
  } else {
    // Not the last thread - go to sleep
    halt(id);
  }
}
