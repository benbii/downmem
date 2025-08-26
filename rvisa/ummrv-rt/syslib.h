#ifndef DMMRV_SYSLIB_H
#define DMMRV_SYSLIB_H

#include <stdint.h>
#include <stddef.h>

// Memory space attributes for the custom hardware
#define __mram __attribute__((used, address_space(255), section(".mram")))
#define __mram_noinit __attribute__((used, address_space(255), section(".mram.noinit")))
#define __mram_ptr __attribute__((address_space(255)))
#define __host __attribute__((used))

// Hardware thread ID intrinsic
static inline uint32_t me(void) {
    uint32_t result;
    __asm__ volatile ("myid %0" : "=r"(result));
    return result;
}

// DMA operations for memory transfers
static inline void mram_read(const __mram_ptr void* src, void* dst, size_t sz) {
    __asm__ volatile ("ldmram %1, %0, %2" : : "r"(src), "r"(dst), "r"(sz) : "memory");
}
static inline void mram_write(const void* src, __mram_ptr void* dst, size_t sz) {
    __asm__ volatile ("sdmram %0, %1, %2" : : "r"(src), "r"(dst), "r"(sz) : "memory");
}

// Thread control operations via CSR manipulation
static inline void halt(uint32_t id) {
    uint32_t mask = 1U << id;
    __asm__ volatile ("csrrc zero, 0x000, %0" : : "r"(mask));
}
static inline void resume(uint32_t id) {
    uint32_t mask = 1U << id;
    __asm__ volatile ("csrrs zero, 0x000, %0" : : "r"(mask));
}

// Memory layout constants
#define SCRATCHPAD_BASE 0x00000000
#define SCRATCHPAD_SIZE 0x00010000  // 64KB
#define MAIN_MEM_BASE   0x08000000  
#define MAIN_MEM_SIZE   0x04000000  // 64MB
#define INSTR_MEM_BASE  0x80000000
#define INSTR_MEM_SIZE  0x00010000  // 64KB

// Runtime configuration - these will be overridden by compile-time defines
#define MAX_NR_TASKLETS 24
#ifndef NR_TASKLETS
#define NR_TASKLETS 1
#endif
#ifndef STACK_SIZE
#define STACK_SIZE 0x0400 
#endif

// Runtime symbols provided by linker script
extern uint8_t __sys_used_scratchpad_end;
extern __attribute__((address_space(255), section(".mram.noinit")))
uint8_t __sys_used_mram_end;
#define DPU_MRAM_HEAP_POINTER &__sys_used_mram_end

#endif // SYSLIB_H
