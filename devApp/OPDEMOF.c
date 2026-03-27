#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <stddef.h>
#include <stdint.h>

__host uint32_t nrWords, nrIter, is64;
// _Static_assert(sizeof(size_t) == 4, "UPMEM 4-bit words!");

__attribute__((noinline)) float demo32(float a, float b) {
  if (b > 2e9) b = 6666.6666;
  if (b < -2e9) b = -6666.6666;
  if (a > 2e9) a = 88888.888;
  if (a < -2e9) a = -88888.888;
  int ib = (int)b;
  a -= (float)ib;
  return (ib & 1) ? a * b : a / b;
}

__attribute__((noinline)) double demo64(double a, double b) {
  a -= b;
  return a * b;
}

int main() {
  uint8_t bufW_[512];
  if (is64 < 64) {
    size_t tlNrWords = (nrWords + NR_TASKLETS - 1) / NR_TASKLETS;
    size_t tlNrBytes = sizeof(float) * tlNrWords;
    __mram_ptr float *opM = DPU_MRAM_HEAP_POINTER + me() * tlNrBytes;
    __mram_ptr float *resM = opM + NR_TASKLETS * tlNrWords;
    float *bufW = (float *)bufW_;
    // float *bufW = &wramBuf[512 / sizeof(float) * me()];

    size_t i = 0;
    for (; i + 128 < tlNrWords; i += 128) {
      mram_read(opM + i, bufW, 512);
      for (size_t j = 0; j < 128; j += 2) {
        float l = bufW[j], r = bufW[j + 1];
        for (size_t k = 0; k < nrIter; ++k)
          l = demo32(l, r), r = demo32(l, r);
        bufW[j] = l, bufW[j + 1] = r;
      }
      mram_write(bufW, resM + i, 512);
    }

    mram_read(opM + i, bufW, (tlNrWords - i) * sizeof(float));
    for (size_t j = 0; j < tlNrWords - i; j += 2) {
      float l = bufW[j], r = bufW[j + 1];
      for (size_t k = 0; k < nrIter; ++k)
        l = demo32(l, r), r = demo32(l, r);
      bufW[j] = l, bufW[j + 1] = r;
    }
    mram_write(bufW, resM + i, (tlNrWords - i) * sizeof(float));
    return 0;
  }

  // 64b
  size_t tlNrWords = (nrWords + NR_TASKLETS - 1) / NR_TASKLETS;
  size_t tlNrBytes = sizeof(double) * tlNrWords;
  __mram_ptr double *opM = DPU_MRAM_HEAP_POINTER + me() * tlNrBytes;
  __mram_ptr double *resM = opM + NR_TASKLETS * tlNrWords;
  double *bufW = (double *)bufW_;

  size_t i = 0;
  for (; i + 64 < tlNrWords; i += 64) {
    mram_read(opM + i, bufW, 512);
    for (size_t j = 0; j < 64; j += 2) {
      double l = bufW[j], r = bufW[j + 1];
      for (size_t k = 0; k < nrIter; ++k)
        l = demo64(l, r), r = demo64(l, r);
      bufW[j] = l, bufW[j + 1] = r;
    }
    mram_write(bufW, resM + i, 512);
  }

  mram_read(opM + i, bufW, (tlNrWords - i) * sizeof(double));
  for (size_t j = 0; j < tlNrWords - i; j += 2) {
    double l = bufW[j], r = bufW[j + 1];
    for (size_t k = 0; k < nrIter; ++k)
      l = demo64(l, r), r = demo64(l, r);
    bufW[j] = l, bufW[j + 1] = r;
  }
  mram_write(bufW, resM + i, (tlNrWords - i) * sizeof(double));
  return 0;
}

