#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <stddef.h>
#include <stdint.h>

__host uint32_t nrWords, nrIter, is64;

__attribute__((noinline))
uint32_t demo32(uint32_t a, uint32_t b) {
  b >>= (b & 7);
  a <<= 2;
  a += b;
  uint32_t rem = b == 0 ? 1 : (int32_t)a / (int32_t)b;
  uint32_t qut = b == 0 ? 1 : (int32_t)a % (int32_t)b;
  if ((a & (3 << 15)) != 0 && b != 0)
    a -= __builtin_clz(b) + __builtin_popcount(b) + __builtin_ctz(b);
  a = __builtin_rotateleft32(a, a & 31);
  b = __builtin_rotateright32(b, b & 31);
  return (a & b) ^ (qut * rem);
}

__attribute__((noinline))
uint64_t demo64(uint64_t a, uint64_t b) {
  b >>= (b & 7);
  a <<= 2;
  a += b;
  uint64_t qut = b == 0 ? 1 : a / b;
  uint64_t rem = b == 0 ? 1 : a % b;
  if ((a & (3ull << 31)) != 0 && b != 0)
    a -= __builtin_clzll(b) + __builtin_ctzll(b) + __builtin_popcountll(b);
  a = __builtin_rotateleft64(a, a & 31);
  b = __builtin_rotateright64(b, b & 31);
  return (a - b) ^ (qut + rem);
  // return qut + rem;
  // return a - b;
}

int main() {
  uint8_t bufW_[512];
  if (is64 < 64) {
    size_t tlNrWords = (nrWords + NR_TASKLETS - 1) / NR_TASKLETS;
    size_t tlNrBytes = sizeof(uint32_t) * tlNrWords;
    __mram_ptr uint32_t *opM = DPU_MRAM_HEAP_POINTER + me() * tlNrBytes;
    __mram_ptr uint32_t *resM = opM + NR_TASKLETS * tlNrWords;
    uint32_t *bufW = (uint32_t *)bufW_;
    // uint32_t *bufW = &wramBuf[512 / sizeof(uint32_t) * me()];

    size_t i = 0;
    // for (; i + 128 < tlNrWords; i += 128) {
    //   mram_read(opM + i, bufW, 512);
    //   for (size_t j = 0; j < 128; j += 2) {
    //     uint32_t l = bufW[j], r = bufW[j + 1];
    //     for (size_t k = 0; k < nrIter; ++k)
    //       l = demo32(l, r), r = demo32(l, r);
    //     bufW[j] = l, bufW[j + 1] = r;
    //   }
    //   mram_write(bufW, resM + i, 512);
    // }

    mram_read(opM + i, bufW, (tlNrWords - i) * sizeof(uint32_t));
    for (size_t j = 0; j < tlNrWords - i; j += 2) {
      uint32_t l = bufW[j], r = bufW[j + 1];
      for (size_t k = 0; k < nrIter; ++k)
        l = demo32(l, r), r = demo32(l, r);
      bufW[j] = l, bufW[j + 1] = r;
    }
    mram_write(bufW, resM + i, (tlNrWords - i) * sizeof(uint32_t));
    return 0;
  }

  // 64b
  size_t tlNrWords = (nrWords + NR_TASKLETS - 1) / NR_TASKLETS;
  size_t tlNrBytes = sizeof(uint64_t) * tlNrWords;
  __mram_ptr uint64_t *opM = DPU_MRAM_HEAP_POINTER + me() * tlNrBytes;
  __mram_ptr uint64_t *resM = opM + NR_TASKLETS * tlNrWords;
  uint64_t *bufW = (uint64_t *)bufW_;

  size_t i = 0;
  // for (; i + 64 < tlNrWords; i += 64) {
  //   mram_read(opM + i, bufW, 512);
  //   for (size_t j = 0; j < 64; j += 2) {
  //     uint64_t l = bufW[j], r = bufW[j + 1];
  //     for (size_t k = 0; k < nrIter; ++k)
  //       l = demo64(l, r), r = demo64(l, r);
  //     bufW[j] = l, bufW[j + 1] = r;
  //   }
  //   mram_write(bufW, resM + i, 512);
  // }

  mram_read(opM + i, bufW, (tlNrWords - i) * sizeof(uint64_t));
  for (size_t j = 0; j < tlNrWords - i; j += 2) {
    uint64_t l = bufW[j], r = bufW[j + 1];
    for (size_t k = 0; k < nrIter; ++k)
      l = demo64(l, r), r = demo64(l, r);
    bufW[j] = l, bufW[j + 1] = r;
  }
  mram_write(bufW, resM + i, (tlNrWords - i) * sizeof(uint64_t));
  return 0;
}

