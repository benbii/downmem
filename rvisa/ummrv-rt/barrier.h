#ifndef BARRIER_H
#define BARRIER_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// DPU barrier for thread synchronization using CSR2 (shared with spinlock)
typedef struct dpu_barrier_t {
  uint8_t nthBitInCsr2;     // CSR2 bit for metadata locking (shared with spinlock)
  uint8_t expected_count;   // Number of threads expected
  uint8_t current_count;    // Current number of arrived threads
  uint8_t pad;
  uint32_t sleeping_mask;   // Bitset of threads sleeping on this barrier
} barrier_t;

#define DPU_BARRIER_INITIALIZER(nthBit, count) {nthBit, count, 0, 0, 0}
#define DPU_BARRIER_INIT(name, nthBit, count) barrier_t name = {nthBit, count, 0, 0, 0}
#define BARRIER_INIT(name, count) DPU_BARRIER_INIT(name, __COUNTER__, count)
void barrier_wait(barrier_t* barrier);

// Known thread barrier using dedicated CSR3 - for predetermined thread sets
// It does not require explicit initialization
void known_barrier_wait(uint32_t thrd_msk);
// Common use case: all threads barrier
#define all_threads_barrier_wait() known_barrier_wait((1u << NR_TASKLETS) - 1)
#define ALL_THREADS_BARRIER_INIT()

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif // BARRIER_H
