#include <stdio.h>
#include <time.h>
#include <dpu.h>
#include <stdlib.h>
#define BRUHNUM 888

struct xferInfo {
  ssize_t xferSz; // <0 for WRAM transfer
  unsigned nrDpu;
  size_t htodNoBr, dtoh, htodBr;
};
static void xferInfoShow(struct xferInfo x) {
  char c = x.xferSz < 0 ? 'W' : 'M';
  if (x.xferSz < 0) x.xferSz = -x.xferSz;
  if (x.xferSz == BRUHNUM) return;
  printf("%c,%u,%zd,%zu,%zu,%zu\n", c, x.nrDpu, x.xferSz, x.htodBr, x.htodNoBr,
         x.dtoh);
  fflush(stdout);
}

static struct xferInfo measureOne(struct dpu_set_t dpuSet, void *buf,
                                  ssize_t xferSz) {
  struct xferInfo res = {.xferSz = xferSz, .htodNoBr = 0, .dtoh = 0, .htodBr = 0};
  struct dpu_set_t eachDpu;
  DPU_FOREACH(dpuSet, eachDpu, res.nrDpu) {
    dpu_prepare_xfer(eachDpu, buf + xferSz * res.nrDpu);
  }

  struct timespec t1, t2;
  const char *ptrName = DPU_MRAM_HEAP_POINTER_NAME;
  if (xferSz < 0) {
    ptrName = "dummyWram";
    xferSz = -xferSz;
  }
  xferSz &= ~7;

  timespec_get(&t1, TIME_UTC);
  DPU_ASSERT(dpu_push_xfer(dpuSet, DPU_XFER_TO_DPU, ptrName, 0, xferSz,
                           DPU_XFER_NO_RESET));
  // dpu_sync(dpuSet);
  timespec_get(&t2, TIME_UTC);
  res.htodNoBr += (t2.tv_nsec - t1.tv_nsec) + 999999999 * (t2.tv_sec - t1.tv_sec);
  DPU_ASSERT(dpu_launch(dpuSet, DPU_SYNCHRONOUS));

  timespec_get(&t1, TIME_UTC);
  DPU_ASSERT(dpu_push_xfer(dpuSet, DPU_XFER_FROM_DPU, ptrName, 0, xferSz,
                           DPU_XFER_NO_RESET));
  timespec_get(&t2, TIME_UTC);
  res.dtoh += (t2.tv_nsec - t1.tv_nsec) + 999999999 * (t2.tv_sec - t1.tv_sec);

  timespec_get(&t1, TIME_UTC);
  DPU_ASSERT(dpu_broadcast_to(dpuSet, ptrName, 0, buf, xferSz, DPU_XFER_NO_RESET));
  // dpu_sync(dpuSet);
  timespec_get(&t2, TIME_UTC);
  res.htodBr += (t2.tv_nsec - t1.tv_nsec) + 999999999 * (t2.tv_sec - t1.tv_sec);
  DPU_ASSERT(dpu_launch(dpuSet, DPU_SYNCHRONOUS));
  return res;
}

static void measureStride(size_t stride, const char* devExec, void* buf) {
  struct dpu_set_t dpuSet;
  for (size_t i = stride; DPU_OK == dpu_alloc(i, NULL, &dpuSet); i += stride) {
    DPU_ASSERT(dpu_load(dpuSet, devExec, NULL));
    (void)measureOne(dpuSet, buf, BRUHNUM);
    (void)measureOne(dpuSet, buf, -BRUHNUM);
    for (ssize_t s = 8; s < 256; s <<= 1) {
      xferInfoShow(measureOne(dpuSet, buf, s));
      xferInfoShow(measureOne(dpuSet, buf, -s));
    }
    for (ssize_t s = 256; s < 2048; s += 128) {
      xferInfoShow(measureOne(dpuSet, buf, s));
      xferInfoShow(measureOne(dpuSet, buf, -s));
    }
    for (size_t s = 2048; s < 1200000; s += (s >> 3))
      xferInfoShow(measureOne(dpuSet, buf, s));
    for (ssize_t s = 2048; s < 10000; s += (s >> 3))
      xferInfoShow(measureOne(dpuSet, buf, -s));
    dpu_free(dpuSet);
  }
}

int main(int argc, char** argv) {
  uint64_t* buf = malloc(2560u * 1024 * 1024);
  for (size_t i = 0; i < 320 * 1024 * 1024; ++i)
    buf[i] = i;

  puts("?RAM,nrDpu,xferSz,hToDBr,hToDNoBr,dToH");
  measureStride(64, argv[1], buf);
  measureStride(32, argv[1], buf);
  measureStride(16, argv[1], buf);
}

