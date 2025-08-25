#include "semaphore.h"
#include "syslib.h"
#include <stdbool.h>

// CSR1 manipulation helpers for semaphore metadata locking (shared with mutex)
static inline uint32_t csr1_test_and_set(uint32_t mask) {
    uint32_t prev;
    __asm__ volatile ("csrrs %0, 0x001, %1" : "=r"(prev) : "r"(mask));
    return prev;
}

static inline void csr1_clear(uint32_t mask) {
    __asm__ volatile ("csrrc zero, 0x001, %0" : : "r"(mask));
}

int sem_init(sem_t* sem, uint8_t nth_bit, uint8_t initial_count) {
    if (nth_bit >= 32 || initial_count == 0) {
        return -1; // Invalid parameters
    }
    
    sem->nthBitInCsr1 = nth_bit;
    sem->max_count = initial_count;
    sem->current_count = initial_count;
    sem->waiting_mask = 0;
    
    return 0;
}

void sem_wait(sem_t* sem) {
    uint32_t msk = 1u << sem->nthBitInCsr1;
    uint32_t id = me();
    uint32_t thread_bit = 1u << id;
    
    // Get exclusive access to semaphore metadata
    while (csr1_test_and_set(msk) & msk) {
        ; // spin waiting for metadata access
    }
    
    if (sem->current_count > 0) {
        // Resource available - decrement and proceed
        sem->current_count--;
        csr1_clear(msk);
    } else {
        // No resources - add self to waiting set and sleep
        sem->waiting_mask |= thread_bit;
        csr1_clear(msk);
        halt(id);
    }
}

void sem_give(sem_t* sem) {
    uint32_t msk = 1u << sem->nthBitInCsr1;
    
    // Get exclusive access to semaphore metadata
    while (csr1_test_and_set(msk) & msk) {
        ; // spin waiting for metadata access
    }
    
    if (sem->waiting_mask != 0) {
        // Wake up one waiting thread
        uint32_t wake_thread = __builtin_ctz(sem->waiting_mask); // Find first set bit
        uint32_t wake_bit = 1u << wake_thread;
        sem->waiting_mask &= ~wake_bit; // Remove from waiting set
        
        csr1_clear(msk);
        
        // Wake the selected thread and ensure it actually sleeps if still running
        uint32_t prev_running;
        __asm__ volatile ("csrrs %0, 0x000, %1" : "=r"(prev_running) : "r"(wake_bit));
        
        // If thread was still running, keep trying to wake it
        while (prev_running & wake_bit) {
            // Small delay to let thread actually sleep
            __asm__ volatile ("addi x0, x0, 0");
            
            // Try waking again
            __asm__ volatile ("csrrs %0, 0x000, %1" : "=r"(prev_running) : "r"(wake_bit));
        }
    } else {
        // No waiting threads - increment count if not at max
        if (sem->current_count < sem->max_count) {
            sem->current_count++;
        }
        csr1_clear(msk);
    }
}

int sem_trywait(sem_t* sem) {
    uint32_t msk = 1u << sem->nthBitInCsr1;
    
    // Try to get exclusive access to semaphore metadata
    if (csr1_test_and_set(msk) & msk) {
        return -1; // Failed to acquire metadata lock
    }
    
    if (sem->current_count > 0) {
        // Resource available - decrement and succeed
        sem->current_count--;
        csr1_clear(msk);
        return 0;
    } else {
        // No resources available - fail without blocking
        csr1_clear(msk);
        return -1;
    }
}