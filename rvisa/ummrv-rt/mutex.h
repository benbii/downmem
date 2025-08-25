#include <stdint.h>
#include <stdbool.h>
#include "syslib.h"
#ifdef __cplusplus
extern "C" {
#endif

// DPU synchoronization types is best statically initialized
typedef struct dpu_sleepmtx_t {
  uint8_t nthBitInCsr2;
  uint8_t head, tail, pad;
  uint8_t queue[MAX_NR_TASKLETS];
} sleepmtx_t;

// Spinlock that busy-waits on CSR1 bits instead of sleeping
// (spinlock is the default mutex in umm bc it's simple as f*ck)
typedef uint8_t mtx_t;

#define DPU_SLEEPMUTEX_INITIALIZER(nthBit) {nthBit, 0, 0, 0, {}}
#define DPU_SLEEPMUTEX_INIT(name, nthBit) \
  sleepmtx_t name = {nthBit, 0, 0, 0, {}}
// Not recommended and errorneous in multi-file DPU app
#define SLEEPMUTEX_INIT(name) DPU_SLEEPMUTEX_INIT(name, __COUNTER__)

#define DPU_SPINMUTEX_INITIALIZER(nthBit) 1 << nthBit
#define DPU_SPINMUTEX_INIT(name, nthBit) mtx_t name = 1 << nthBit
// Not recommended and errorneous in multi-file DPU app
#define MUTEX_INIT(name) DPU_SPINMUTEX_INIT(name, __COUNTER__)

void sleepmutex_lock(sleepmtx_t* mtx);
void sleepmutex_unlock(sleepmtx_t* mtx);
bool sleepmutex_trylock(sleepmtx_t* mtx);

// CSR manipulation helpers for spinlock control (CSR1)
static inline uint32_t csr1_test_and_set(uint32_t mask) {
  uint32_t prev;
  __asm__ volatile("csrrs %0, 0x001, %1" : "=r"(prev) : "r"(mask));
  return prev;
}

// Spinlock implementations using CSR1 (moved inline for efficiency)
static inline void mutex_lock(mtx_t msk) {
  // Atomic test-and-set until we acquire the spinlock
  while (csr1_test_and_set(msk) & msk) ;
}

static inline void mutex_unlock(mtx_t msk) {
  __asm__ volatile("csrrc zero, 0x001, %0" : : "r"(msk));
}

static inline bool mutex_trylock(mtx_t msk) {
  // Single atomic test-and-set attempt
  if (csr1_test_and_set(msk) & msk) return false;
  return true; // Success (bit was 0, now set to 1)
}

#ifdef __cplusplus
} /* extern "C" */
#endif
