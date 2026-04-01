// UPMEM-TRANSLATE: COMPILE_mv_dimm4_opt:1:mv_dimm4_opt;COMPILE_mv_dimm8_nopt:1:mv_dimm8_nopt;COMPILE_mv_dimm16_opt:1:mv_dimm16_opt;

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <perfcounter.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "expf.c"

#ifdef COMPILE_mv_dimm4_opt
void mv_dimm4_opt() {
  int32_t v1 = (uint32_t) DPU_MRAM_HEAP_POINTER;
  int32_t v2 = me();
  int32_t v3 = v2 * 8192;
  int32_t v4 = v1 + v3;
  __dma_aligned int32_t v5[2048];
  int32_t v6 = v1 + 8192;
  int32_t v7 = v6 + v3;
  __dma_aligned int32_t v8[2048];
  int32_t v9 = v6 + 8192;
  int32_t v10 = v2 * 8;
  int32_t v11 = v9 + v10;
  __dma_aligned int32_t v12[2];
  mram_read((const __mram_ptr int32_t*)v4, (int32_t*)v5, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v4 + 512, (int32_t*)v5 + 512, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v4 + 1024, (int32_t*)v5 + 1024, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v4 + 1536, (int32_t*)v5 + 1536, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7, (int32_t*)v8, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7 + 512, (int32_t*)v8 + 512, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7 + 1024, (int32_t*)v8 + 1024, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7 + 1536, (int32_t*)v8 + 1536, 512 * sizeof(int32_t));
  for (int32_t v13 = 0; v13 < 2048; v13 += 1) {
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

#ifdef COMPILE_mv_dimm8_nopt
void mv_dimm8_nopt() {
  int32_t v1 = (uint32_t) DPU_MRAM_HEAP_POINTER;
  int32_t v2 = me();
  int32_t v3 = v2 * 4104;
  int32_t v4 = v1 + v3;
  __dma_aligned int32_t v5[512];
  int32_t v6 = v1 + 2048;
  int32_t v7 = v6 + v3;
  __dma_aligned int32_t v8[512];
  int32_t v9 = v6 + 2048;
  int32_t v10 = v2 * 8;
  int32_t v11 = v7 + 2048;
  __dma_aligned int32_t v12[2];
  mram_read((const __mram_ptr int32_t*)v4, (int32_t*)v5, 512 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7, (int32_t*)v8, 512 * sizeof(int32_t));
  for (int32_t v13 = 0; v13 < 512; v13 += 1) {
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

#ifdef COMPILE_mv_dimm16_opt
void mv_dimm16_opt() {
  int32_t v1 = (uint32_t) DPU_MRAM_HEAP_POINTER;
  int32_t v2 = me();
  int32_t v3 = v2 * 512;
  int32_t v4 = v1 + v3;
  __dma_aligned int32_t v5[128];
  int32_t v6 = v1 + 512;
  int32_t v7 = v6 + v3;
  __dma_aligned int32_t v8[128];
  int32_t v9 = v6 + 512;
  int32_t v10 = v2 * 8;
  int32_t v11 = v9 + v10;
  __dma_aligned int32_t v12[2];
  mram_read((const __mram_ptr int32_t*)v4, (int32_t*)v5, 128 * sizeof(int32_t));
  mram_read((const __mram_ptr int32_t*)v7, (int32_t*)v8, 128 * sizeof(int32_t));
  for (int32_t v13 = 0; v13 < 128; v13 += 1) {
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
#ifdef COMPILE_mv_dimm4_opt
  mv_dimm4_opt();
#endif
#ifdef COMPILE_mv_dimm8_nopt
  mv_dimm8_nopt();
#endif
#ifdef COMPILE_mv_dimm16_opt
  mv_dimm16_opt();
#endif
  mem_reset();
  return 0;
}
