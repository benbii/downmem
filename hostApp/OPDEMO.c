#include <dpu.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#ifdef __clang__
#define rotl64c __builtin_rotateleft64
#define rotr64c __builtin_rotateright64
#define rotl32c __builtin_rotateleft32
#define rotr32c __builtin_rotateright32
#else
static uint64_t rotl64c (uint64_t x, uint64_t n) {
  assert (n<64);
  return (x<<n) | (x>>(-n&63));
}
static uint64_t rotr64c (uint64_t x, uint64_t n) {
  assert (n<64);
  return (x>>n) | (x<<(-n&63));
}
static uint32_t rotl32c (uint32_t x, uint32_t n) {
  assert (n<32);
  return (x<<n) | (x>>(-n&31));
}
static uint32_t rotr32c (uint32_t x, uint32_t n) {
  assert (n<32);
  return (x>>n) | (x<<(-n&31));
}
#endif

static uint32_t demo32(uint32_t a, uint32_t b) {
  b >>= (b & 7);
  a <<= 2;
  a += b;
  uint32_t rem = b == 0 ? 1 : (int32_t)a / (int32_t)b;
  uint32_t qut = b == 0 ? 1 : (int32_t)a % (int32_t)b;
  if ((a & (3 << 15)) != 0)
    a -= __builtin_clz(b) + __builtin_popcount(b) + __builtin_ctz(b);
  a = rotl32c(a, a & 31);
  b = rotr32c(b, b & 31);
  return (a & b) ^ (qut * rem);
}

static uint64_t demo64(uint64_t a, uint64_t b) {
  b >>= (b & 7);
  a <<= 2;
  a += b;
  uint64_t qut = b == 0 ? 1 : a / b;
  uint64_t rem = b == 0 ? 1 : a % b;
  if ((a & (3ull << 31)) != 0 && b != 0)
    a -= __builtin_clzll(b) + __builtin_ctzll(b) + __builtin_popcountll(b);
  a = rotl64c(a, a & 31);
  b = rotr64c(b, b & 31);
  return (a - b) ^ (qut + rem);
  // return qut + rem;
  // return a - b;
}

// TODO: dynamically determine NR_TASKLETS
#ifndef NR_TASKLETS
#define NR_TASKLETS 16
#endif

// build/benchOpdemo 1024 1 OPDEMO.objdump 1
int main(int argc, char** argv) {
  struct dpu_set_t dpuSet, dpu;
  size_t totWords = atoi(argv[1]), nrIter = atoi(argv[4]),
         nrDpus = atoi(argv[2]), i;
  if (totWords % (2 * NR_TASKLETS * nrDpus) != 0) {
    totWords =
        (1 + totWords / (2 * NR_TASKLETS * nrDpus)) * 2 * NR_TASKLETS * nrDpus;
    fprintf(stderr, "Element per DPU must be a multiple of 2*NrTasklet; "
            "wrapping to %zu elements total\n", totWords);
  }
  const size_t nrWords = totWords / nrDpus;
  DPU_ASSERT(dpu_alloc(nrDpus, NULL, &dpuSet));
  DPU_ASSERT(dpu_load(dpuSet, argv[3], NULL));

  uint32_t* inputs = malloc(totWords * sizeof(uint64_t));
  uint32_t* hostRes = malloc(totWords * sizeof(uint64_t));
  uint32_t* devRes = malloc(totWords * sizeof(uint64_t));
  srand(12345678);
  for (size_t i = 0; i < totWords; ++i) {
    switch (i & 7) {
    case 0: case 1: case 3: case 4:
      inputs[i] = rand(); break;
    case 2: case 5: case 6: case 7:
      inputs[i] = -rand(); break;
    }
  }

// 32b demo on CPU. Currently using omp with DMM is fine *as long as OMP block
// contain no DPU operations*! DPU+OMP might be supported in the future :)
#pragma omp parallel for schedule(static, 1024)
  for (size_t i = 0; i < totWords; i += 2) {
    uint32_t l = inputs[i], r = inputs[i + 1];
    for (size_t j = 0; j < nrIter; ++j) {
      l = demo32(l, r); r = demo32(l, r);
    }
    hostRes[i] = l, hostRes[i + 1] = r;
    devRes[i] = devRes[i + 1] = 0x87654321;
  }

  for (size_t n = 0; n < 4; ++n) {
    dpu_broadcast_to(dpuSet, "nrWords", 0, &nrWords, sizeof(nrWords),
                     DPU_XFER_DEFAULT);
    dpu_broadcast_to(dpuSet, "nrIter", 0, &nrIter, sizeof(nrIter),
                     DPU_XFER_DEFAULT);
    const uint32_t bruh = 32;
    dpu_broadcast_to(dpuSet, "is64", 0, &bruh, sizeof(nrIter),
                     DPU_XFER_DEFAULT);
    DPU_FOREACH(dpuSet, dpu, i) { dpu_prepare_xfer(dpu, inputs + nrWords * i); }
    dpu_push_xfer(dpuSet, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                  nrWords * sizeof(uint32_t), DPU_XFER_DEFAULT);
    dpu_launch(dpuSet, DPU_SYNCHRONOUS);
    DPU_FOREACH(dpuSet, dpu, i) { dpu_prepare_xfer(dpu, devRes + nrWords * i); }
    dpu_push_xfer(dpuSet, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                  nrWords * sizeof(uint32_t), nrWords * sizeof(uint32_t),
                  DPU_XFER_DEFAULT);
    for (size_t i = 0; i < nrWords; ++i)
      if (devRes[i] != hostRes[i])
        exit(fprintf(stderr, "32b BAD at %zu, h=%u d=%u\n",
                     i, hostRes[i], devRes[i]));
  }

  uint64_t* inputs6 = (uint64_t*)inputs;
  uint64_t* hostRes6 = (uint64_t*)hostRes;
  uint64_t* devRes6 = (uint64_t*)devRes;
  for (size_t i = 0; i < totWords; ++i) {
    // int64_t val = ((i & 0x3fffffffll) << 30) + (i & 0x3fffffffll);
    int64_t val = ((rand() & 0x3fffffffll) << 30) + (rand() & 0x3fffffffll);
    // int64_t val = i;
    // inputs6[i] = (uint64_t)val;
    switch (i & 7) {
    case 0: case 1: case 3: case 4:
      inputs6[i] = val; break;
    case 2: case 5: case 6: case 7:
      inputs6[i] = -val; break;
    }
  }

  // 64b demo on CPU
#pragma omp parallel for schedule(static, 1024)
  for (size_t i = 0; i < totWords; i += 2) {
    uint64_t l = inputs6[i], r = inputs6[i + 1];
    for (size_t j = 0; j < nrIter; ++j) {
      l = demo64(l, r); r = demo64(l, r);
    }
    hostRes6[i] = l, hostRes6[i + 1] = r;
    devRes6[i] = devRes6[i + 1] = 0x87654321;
  }

  for (size_t n = 0; n < 4; ++n) {
    // using DPU_INPUT_ARGUMENTS as is in many prim programs is indeed faster
    dpu_broadcast_to(dpuSet, "nrWords", 0, &nrWords, sizeof(nrWords),
                     DPU_XFER_DEFAULT);
    dpu_broadcast_to(dpuSet, "nrIter", 0, &nrIter, sizeof(nrIter),
                     DPU_XFER_DEFAULT);
    const uint32_t bruh = 64;
    dpu_broadcast_to(dpuSet, "is64", 0, &bruh, sizeof(bruh),
                     DPU_XFER_DEFAULT);
    DPU_FOREACH(dpuSet, dpu, i) { dpu_prepare_xfer(dpu, inputs6 + nrWords * i); }
    dpu_push_xfer(dpuSet, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                  nrWords * sizeof(uint64_t), DPU_XFER_DEFAULT);
    dpu_launch(dpuSet, DPU_SYNCHRONOUS);
    DPU_FOREACH(dpuSet, dpu, i) { dpu_prepare_xfer(dpu, devRes6 + nrWords * i); }
    dpu_push_xfer(dpuSet, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                  nrWords * sizeof(uint64_t), nrWords * sizeof(uint64_t),
                  DPU_XFER_DEFAULT);
    for (size_t i = 0; i < nrWords; ++i)
      if (hostRes6[i] != devRes6[i])
        exit(fprintf(stderr, "64b BAD at %zu, h=%lx d=%lx\n",
                     i, hostRes6[i], devRes6[i]));
  }
  dpu_free(dpuSet);
  return 0;
}

