#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <stdint.h>
#include "syslib.h"
#ifdef __cplusplus
extern "C" {
#endif

// DPU semaphore for counting synchronization using CSR1 (shared with mutex)
typedef struct dpu_sem_t {
  uint8_t nthBitInCsr1;     // CSR1 bit for metadata locking (shared with mutex)
  uint8_t max_count;        // Maximum semaphore count
  uint8_t current_count;    // Current semaphore count
  uint8_t pad;
  uint32_t waiting_mask;    // Bitset of threads waiting on this semaphore
} sem_t;

#define DPU_SEMAPHORE_INITIALIZER(nthBit, count) {nthBit, count, count, 0, 0}
#define DPU_SEMAPHORE_INIT(name, nthBit, count) sem_t name = {nthBit, count, count, 0, 0}

void sem_wait(sem_t* sem);
void sem_give(sem_t* sem);
int sem_trywait(sem_t* sem);
int sem_init(sem_t* sem, uint8_t nth_bit, uint8_t initial_count);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif // SEMAPHORE_H