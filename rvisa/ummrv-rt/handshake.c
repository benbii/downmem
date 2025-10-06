#include "handshake.h"
#include "syslib.h"

static int32_t slots[24];

// CSR7 manipulation helpers for handshake slot locking
static inline uint32_t csr7_test_and_set(uint32_t mask) {
  uint32_t prev;
  __asm__ volatile("csrrs %0, 0x007, %1" : "=r"(prev) : "r"(mask));
  return prev;
}

static inline void csr7_clear(uint32_t mask) {
  __asm__ volatile("csrrc zero, 0x007, %0" : : "r"(mask));
}

int handshake_wait_for(sysname_t idx) {
  uint32_t lock_bit = 1u << idx;
  uint32_t my_id = me();

  // Lock this slot
  while (csr7_test_and_set(lock_bit) & lock_bit)
    ;

  int32_t a = slots[idx];

  if (a == 0) {
    // First to arrive - mark as waiting
    slots[idx] = (int32_t)(my_id + 1);
    csr7_clear(lock_bit);
    halt(my_id);
    return 0;
  } else if (a > 0) {
    // Someone already waiting - error
    csr7_clear(lock_bit);
    return -1;
  } else {
    // Notifier already arrived - wake them and consume notification
    uint32_t tid = (uint32_t)(-a - 1);
    slots[idx] = 0;
    csr7_clear(lock_bit);

    // Wake the notifier with retry loop
    uint32_t wake_bit = 1u << tid;
    uint32_t prev;
    __asm__ volatile("csrrs %0, 0x000, %1" : "=r"(prev) : "r"(wake_bit));
    while (prev & wake_bit) {
      __asm__ volatile("addi x0, x0, 0");
      __asm__ volatile("csrrs %0, 0x000, %1" : "=r"(prev) : "r"(wake_bit));
    }
    return 0;
  }
}

int handshake_notify_for(sysname_t idx) {
  uint32_t lock_bit = 1u << idx;
  uint32_t my_id = me();

  // Lock this slot
  while (csr7_test_and_set(lock_bit) & lock_bit)
    ;

  int32_t a = slots[idx];

  if (a == 0) {
    // First to arrive - mark as notifying
    slots[idx] = -(int32_t)(my_id + 1);
    csr7_clear(lock_bit);
    halt(my_id);
    return 0;
  } else if (a < 0) {
    // Someone already notifying - error
    csr7_clear(lock_bit);
    return -1;
  } else {
    // Waiter already arrived - wake them and consume wait
    uint32_t tid = (uint32_t)(a - 1);
    slots[idx] = 0;
    csr7_clear(lock_bit);

    // Wake the waiter with retry loop
    uint32_t wake_bit = 1u << tid;
    uint32_t prev;
    __asm__ volatile("csrrs %0, 0x000, %1" : "=r"(prev) : "r"(wake_bit));
    while (prev & wake_bit) {
      __asm__ volatile("addi x0, x0, 0");
      __asm__ volatile("csrrs %0, 0x000, %1" : "=r"(prev) : "r"(wake_bit));
    }
    return 0;
  }
}

void handshake_notify(void) {
  uint32_t my_id = me();
  uint32_t lock_bit = 1u << my_id;

  // Lock this slot
  while (csr7_test_and_set(lock_bit) & lock_bit)
    ;

  int32_t a = slots[my_id];

  if (a == 0) {
    // First to arrive - mark as notifying
    slots[my_id] = -(int32_t)(my_id + 1);
    csr7_clear(lock_bit);
    // Halt using the same lock_bit mask (optimization)
    __asm__ volatile("csrrc zero, 0x000, %0" : : "r"(lock_bit));
  } else {
    // Waiter already arrived (a > 0) - wake them and consume wait
    // No error check for a < 0 since this is the exclusive notify path
    uint32_t tid = (uint32_t)(a - 1);
    slots[my_id] = 0;
    csr7_clear(lock_bit);

    // Wake the waiter with retry loop
    uint32_t wake_bit = 1u << tid;
    uint32_t prev;
    __asm__ volatile("csrrs %0, 0x000, %1" : "=r"(prev) : "r"(wake_bit));
    while (prev & wake_bit) {
      __asm__ volatile("addi x0, x0, 0");
      __asm__ volatile("csrrs %0, 0x000, %1" : "=r"(prev) : "r"(wake_bit));
    }
  }
}
