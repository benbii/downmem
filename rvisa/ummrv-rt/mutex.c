#include "mutex.h"
#include "syslib.h"
#include <stdbool.h>

// CSR manipulation helpers for sleep mutex control (CSR2)
static inline uint32_t csr2_test_and_set(uint32_t mask) {
  uint32_t prev;
  __asm__ volatile("csrrs %0, 0x002, %1" : "=r"(prev) : "r"(mask));
  return prev;
}
static inline void csr2_clear(uint32_t mask) {
  __asm__ volatile("csrrc zero, 0x002, %0" : : "r"(mask));
}

void sleepmutex_lock(sleepmtx_t *mtx) {
  uint32_t msk = 1u << mtx->nthBitInCsr2;
  uint32_t id = me();
  // Get exclusive access to mutex internals using atomic test-and-set
  while (csr2_test_and_set(msk) & msk)
    ;
  bool already_locked = (mtx->head != mtx->tail);
  // Add current thread to queue
  mtx->queue[mtx->tail] = id;
  mtx->tail++;
  if (mtx->tail == MAX_NR_TASKLETS)
    mtx->tail = 0;
  // Release mutex metadata lock
  csr2_clear(msk);
  if (already_locked) {
    // Suspend this thread until woken up
    halt(id);
  }
}

void sleepmutex_unlock(sleepmtx_t *mtx) {
  uint32_t msk = 1u << mtx->nthBitInCsr2;
  // Get exclusive access to mutex internals
  while (csr2_test_and_set(msk) & msk)
    ;
  // Verify the unlocking thread owns the mutex
  // assert(mtx->queue[mtx->head] == me());
  // Remove current thread from queue
  mtx->head++;
  if (mtx->head == MAX_NR_TASKLETS)
    mtx->head = 0;
  // Wake up next thread in queue if any
  if (mtx->head != mtx->tail) {
    uint32_t next_thread = mtx->queue[mtx->head];
    uint32_t wake_bit = 1u << next_thread;
    // Release mutex metadata lock first
    csr2_clear(msk);
    // Wake the next thread and ensure it actually sleeps if still running
    uint32_t prev_running;
    __asm__ volatile("csrrs %0, 0x000, %1"
                     : "=r"(prev_running)
                     : "r"(wake_bit));
    // If thread was still running, keep trying to wake it
    while (prev_running & wake_bit) {
      // Small delay to let thread actually sleep
      __asm__ volatile("addi x0, x0, 0");
      // Try waking again
      __asm__ volatile("csrrs %0, 0x000, %1"
                       : "=r"(prev_running)
                       : "r"(wake_bit));
    }
  } else {
    // Release mutex metadata lock
    csr2_clear(msk);
  }
}

bool sleepmutex_trylock(sleepmtx_t *mtx) {
  uint32_t msk = 1u << mtx->nthBitInCsr2;
  uint32_t id = me();
  // Try to get exclusive access to mutex internals
  if (csr2_test_and_set(msk) & msk) return false;
  if (mtx->head != mtx->tail) {
    // Mutex is locked, release metadata lock and fail
    csr2_clear(msk);
    return false;
  }
  // Mutex is free, add ourselves to queue
  mtx->queue[mtx->tail] = id;
  mtx->tail++;
  if (mtx->tail == MAX_NR_TASKLETS)
    mtx->tail = 0;
  // Release mutex metadata lock
  csr2_clear(msk);
  return true; // Success
}
