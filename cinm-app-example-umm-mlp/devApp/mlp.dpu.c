// UPMEM-TRANSLATE: COMPILE_mm_dimm1_nopt:1:mm_dimm1_nopt;COMPILE_mm_dimm1_opt:1:mm_dimm1_opt;

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <perfcounter.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "expf.c"

#ifdef COMPILE_mm_dimm1_nopt
void mm_dimm1_nopt() {
  int32_t v1 = (uint32_t) DPU_MRAM_HEAP_POINTER;
  int32_t v2 = me();
  int32_t v3 = v2 * 4096;
  int32_t v4 = v1 + v3;
  __dma_aligned int32_t v5[1024];
  int32_t v6 = v1 + 4096;
  int32_t v7 = v6 + v3;
  __dma_aligned int32_t v8[1024];
  int32_t v9 = v6 + 4096;
  int32_t v10 = v2 * 8;
  int32_t v11 = v9 + v10;
  __dma_aligned int32_t v12[2];
  mram_read((const __mram_ptr int32_t*)v4, (int32_t*)v5, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v4 + 512, (int32_t*)v5 + 512, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7, (int32_t*)v8, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7 + 512, (int32_t*)v8 + 512, 512 * sizeof(int32_t));
  for (int32_t v13 = 0; v13 < 1024; v13 += 1) {
    int32_t v14 = v5[v13];
    int32_t v15 = v8[v13];
    int32_t v16 = v12[0];
    int32_t v17 = v14 * v15;
    int32_t v18 = v16 + v17;
    v12[0] = v18;
  }
  mram_write((const int32_t*)v12, (__mram_ptr int32_t*)v11, 2 * sizeof(int32_t));
  return;
}
#endif

#ifdef COMPILE_mm_dimm1_opt
void mm_dimm1_opt() {
  int32_t v1 = (uint32_t) DPU_MRAM_HEAP_POINTER;
  int32_t v2 = me();
  int32_t v3 = v2 * 256;
  int32_t v4 = v1 + v3;
  __dma_aligned int32_t v5[64];
  int32_t v6 = v1 + 256;
  int32_t v7 = v6 + v3;
  __dma_aligned int32_t v8[64];
  int32_t v9 = v6 + 256;
  int32_t v10 = v2 * 8;
  int32_t v11 = v9 + v10;
  __dma_aligned int32_t v12[2];
  mram_read((const __mram_ptr int32_t*)v4, (int32_t*)v5, 64 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7, (int32_t*)v8, 64 * sizeof(int32_t));
  for (int32_t v13 = 0; v13 < 64; v13 += 1) {
    int32_t v14 = v5[v13];
    int32_t v15 = v8[v13];
    int32_t v16 = v12[0];
    int32_t v17 = v14 * v15;
    int32_t v18 = v16 + v17;
    v12[0] = v18;
  }
  mram_write((const int32_t*)v12, (__mram_ptr int32_t*)v11, 2 * sizeof(int32_t));
  return;
}
#endif

BARRIER_INIT(my_barrier, NR_TASKLETS);

int main(void) {
  barrier_wait(&my_barrier);
  mem_reset();
#ifdef COMPILE_mm_dimm1_nopt
  mm_dimm1_nopt();
#endif
#ifdef COMPILE_mm_dimm1_opt
  mm_dimm1_opt();
#endif
  mem_reset();
  return 0;
}
