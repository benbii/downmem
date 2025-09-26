#ifndef DOWNMEM_H
#define DOWNMEM_H

#include <stdbool.h>
#include "rvisa/dmminternal.h"
#include "upmemisa/dmminternal.h"

enum DmmDpuIs {
  UNINIT_DPUIS = 0,
  UMM_DPUIS = 1,
  RV_DPUIS = 2,
};

// Unified DPU structure that can handle both ISAs
struct DmmDpu {
  enum DmmDpuIs Is;
  union {
    UmmDpu U;  // UPMEM DPU
    RvDpu R;   // RISC-V DPU
  };
};

#endif // DOWNMEM_H
