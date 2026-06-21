// UPMEM-TRANSLATE: COMPILE_va_8:16:va_8;COMPILE_va_16:8:va_16;

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <perfcounter.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "expf.c"

#ifdef COMPILE_va_8
void va_8() {
  int32_t v1 = (uint32_t) DPU_MRAM_HEAP_POINTER;
  int32_t v2 = me();
  int32_t v3 = v2 * 1024;
  int32_t v4 = v1 + v3;
  __dma_aligned int32_t v5[256];
  int32_t v6 = v1 + 16384;
  int32_t v7 = v6 + v3;
  __dma_aligned int32_t v8[256];
  int32_t v9 = v6 + 16384;
  int32_t v10 = v9 + v3;
  __dma_aligned int32_t v11[256];
  mram_read((const __mram_ptr int32_t*)v4, (int32_t*)v5, 256 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7, (int32_t*)v8, 256 * sizeof(int32_t));
  for (int32_t v12 = 0; v12 < 256; v12 += 1) {
    int32_t v13 = v5[v12];
    int32_t v14 = v8[v12];
    int32_t v15 = v13 + v14;
    v11[v12] = v15;
  }
  mram_write((const int32_t*)v11, (__mram_ptr int32_t*)v10, 256 * sizeof(int32_t));
  return;
}
#endif

#ifdef COMPILE_va_16
void va_16() {
  int32_t v1 = (uint32_t) DPU_MRAM_HEAP_POINTER;
  int32_t v2 = me();
  int32_t v3 = v2 * 2048;
  int32_t v4 = v1 + v3;
  __dma_aligned int32_t v5[512];
  int32_t v6 = v1 + 16384;
  int32_t v7 = v6 + v3;
  __dma_aligned int32_t v8[512];
  int32_t v9 = v6 + 16384;
  int32_t v10 = v9 + v3;
  __dma_aligned int32_t v11[512];
  mram_read((const __mram_ptr int32_t*)v4, (int32_t*)v5, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7, (int32_t*)v8, 512 * sizeof(int32_t));
  for (int32_t v12 = 0; v12 < 512; v12 += 1) {
    int32_t v13 = v5[v12];
    int32_t v14 = v8[v12];
    int32_t v15 = v13 + v14;
    v11[v12] = v15;
  }
  mram_write((const int32_t*)v11, (__mram_ptr int32_t*)v10, 512 * sizeof(int32_t));
  return;
}
#endif

BARRIER_INIT(my_barrier, NR_TASKLETS);

int main(void) {
  barrier_wait(&my_barrier);
  mem_reset();
#ifdef COMPILE_va_8
  va_8();
#endif
#ifdef COMPILE_va_16
  va_16();
#endif
  mem_reset();
  return 0;
}
