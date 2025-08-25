#include <dpu.h>
#include <math.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static float demo32(float a, float b) {
  if (b > 2e9) b = 6666.6666;
  if (b < -2e9) b = -6666.6666;
  if (a > 2e9) a = 88888.888;
  if (a < -2e9) a = -88888.888;
  int ib = (int)b;
  a -= (float)ib;
  return (ib & 1) ? a * b : a / b;
}

// debugging f64 is so fricking cringe 🥲 better not do it
static double demo64(double a, double b) {
  a -= b;
  return a * b;
}

// TODO: dynamically determine NR_TASKLETS
#ifndef NR_TASKLETS
#define NR_TASKLETS 16
#endif

// build/dmmOpdemof 1024 1 OPDEMOF.objdump 1
int main(int argc, char** argv) {
  struct dpu_set_t dpuSet, dpu;
  size_t totWords = atoi(argv[1]), nrDpus = atoi(argv[2]), i;
  uint32_t nrIter = atoi(argv[4]);
  if (totWords % (2 * NR_TASKLETS * nrDpus) != 0) {
    totWords =
        (1 + totWords / (2 * NR_TASKLETS * nrDpus)) * 2 * NR_TASKLETS * nrDpus;
    fprintf(stderr, "Element per DPU must be a multiple of 2*NrTasklet; "
            "wrapping to %zu elements total\n", totWords);
  }
  const uint32_t nrWords = totWords / nrDpus;
  DPU_ASSERT(dpu_alloc(nrDpus, NULL, &dpuSet));
  DPU_ASSERT(dpu_load(dpuSet, argv[3], NULL));

  float* inputs = malloc(totWords * sizeof(double));
  float* hostRes = malloc(totWords * sizeof(double));
  float* devRes = malloc(totWords * sizeof(double));
  srand(12345678);
  for (size_t i = 0; i < totWords; ++i) {
    // inputs[i] = 0.3 * i;
    switch (i & 7) {
    case 0: case 1: case 3: case 4:
      inputs[i] = rand() / 10000.0; break;
    case 2: case 5: case 6: case 7:
      inputs[i] = -rand() / 12345.6; break;
    }
  }

// 32b demo on CPU. Currently using omp with DMM is fine *as long as OMP block
// contain no DPU operations*! DPU+OMP might be supported in the future :)
// #pragma omp parallel for schedule(static, 1024)
  for (size_t i = 0; i < totWords; i += 2) {
    float l = inputs[i], r = inputs[i + 1];
    for (size_t j = 0; j < nrIter; ++j) {
      l = demo32(l, r); r = demo32(l, r);
    }
    hostRes[i] = l, hostRes[i + 1] = r;
    devRes[i] = devRes[i + 1] = 1234.5678;
  }

  dpu_broadcast_to(dpuSet, "nrWords", 0, &nrWords, sizeof(nrWords),
                    DPU_XFER_DEFAULT);
  dpu_broadcast_to(dpuSet, "nrIter", 0, &nrIter, sizeof(nrIter),
                    DPU_XFER_DEFAULT);
  const uint32_t bruh = 32;
  dpu_broadcast_to(dpuSet, "is64", 0, &bruh, sizeof(nrIter),
                    DPU_XFER_DEFAULT);
  DPU_FOREACH(dpuSet, dpu, i) { dpu_prepare_xfer(dpu, inputs + nrWords * i); }
  dpu_push_xfer(dpuSet, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                nrWords * sizeof(float), DPU_XFER_DEFAULT);
  dpu_launch(dpuSet, DPU_SYNCHRONOUS);
  DPU_FOREACH(dpuSet, dpu, i) { dpu_prepare_xfer(dpu, devRes + nrWords * i); }
  dpu_push_xfer(dpuSet, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                nrWords * sizeof(float), nrWords * sizeof(float),
                DPU_XFER_DEFAULT);
  for (size_t i = 0; i < nrWords; ++i)
    if (devRes[i] / hostRes[i] > 1.01 || devRes[i] / hostRes[i] < .99)
      exit(fprintf(stderr, "32b FBAD at %zu, h=%f d=%f\n",
                    i, hostRes[i], devRes[i]));

  double* inputs6 = (double*)inputs;
  double* hostRes6 = (double*)hostRes;
  double* devRes6 = (double*)devRes;
  for (size_t i = 0; i < totWords; ++i) {
    // inputs6[i] = 0.3 * i;
    switch (i & 7) {
    case 0: case 1: case 3: case 4:
      inputs6[i] = rand() / 10000.0; break;
    case 2: case 5: case 6: case 7:
      inputs6[i] = -rand() / 12345.6; break;
    }
  }

  // 64b demo on CPU
#pragma omp parallel for schedule(static, 1024)
  for (size_t i = 0; i < totWords; i += 2) {
    double l = inputs6[i], r = inputs6[i + 1];
    for (size_t j = 0; j < nrIter; ++j) {
      l = demo64(l, r); r = demo64(l, r);
    }
    hostRes6[i] = l, hostRes6[i + 1] = r;
    devRes6[i] = devRes6[i + 1] = 8765.4321;
  }

  // using DPU_INPUT_ARGUMENTS as is in many prim programs is indeed faster
  dpu_broadcast_to(dpuSet, "nrWords", 0, &nrWords, sizeof(nrWords),
                    DPU_XFER_DEFAULT);
  dpu_broadcast_to(dpuSet, "nrIter", 0, &nrIter, sizeof(nrIter),
                    DPU_XFER_DEFAULT);
  const uint32_t bah = 64;
  dpu_broadcast_to(dpuSet, "is64", 0, &bah, sizeof(bah),
                    DPU_XFER_DEFAULT);
  DPU_FOREACH(dpuSet, dpu, i) { dpu_prepare_xfer(dpu, inputs6 + nrWords * i); }
  dpu_push_xfer(dpuSet, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                nrWords * sizeof(double), DPU_XFER_DEFAULT);
  dpu_launch(dpuSet, DPU_SYNCHRONOUS);
  DPU_FOREACH(dpuSet, dpu, i) { dpu_prepare_xfer(dpu, devRes6 + nrWords * i); }
  dpu_push_xfer(dpuSet, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                nrWords * sizeof(double), nrWords * sizeof(double),
                DPU_XFER_DEFAULT);
  for (size_t i = 0; i < nrWords; ++i)
    if (devRes6[i] / hostRes6[i] > 1.01 || devRes6[i] / hostRes6[i] < .99)
      exit(fprintf(stderr, "64b FBAD at %zu, h=%f d=%f\n",
                    i, hostRes6[i], devRes6[i]));
  dpu_free(dpuSet);
  return 0;
}

