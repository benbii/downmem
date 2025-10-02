#define _GNU_SOURCE
#ifdef __DMM_NUMA
#include <numa.h>
#include <numaif.h>
#include <sched.h>
#endif

#include "dpu.h"
#include "downmem.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static size_t nrCore;
static size_t dmmDpuSize;
static size_t logicFreq;
static size_t memFreq;
static char* dumpFile;
static inline struct DmmDpu* _dptr(size_t i, struct dpu_set_t s) {
  return (struct DmmDpu*)((uintptr_t)s.dmm_dpu + dmmDpuSize * i);
}

dpu_error_t dpu_alloc(uint32_t nrDpu, const char *_, struct dpu_set_t *set) {
  if (set == NULL) return DPU_ERR_ALLOCATION;
  if (nrDpu == 0) return DPU_ERR_ALLOCATION;
  // allocate memory round-robin across cores
  set->dmm_dpu = mmap(NULL, dmmDpuSize * nrDpu, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (set->dmm_dpu == MAP_FAILED) return DPU_ERR_ALLOCATION;
  if (madvise(set->dmm_dpu, dmmDpuSize * nrDpu, MADV_NOHUGEPAGE) != 0)
    perror("madvise");

#ifdef __DMM_NUMA
  // Bind each DmmDpu structure to the NUMA node of its assigned CPU core
  struct bitmask *mask = numa_allocate_nodemask();
  for (size_t i = 0; i < nrDpu; ++i) {
    size_t coreId = i % nrCore;
    int numaNode = numa_node_of_cpu(coreId);
    if (numaNode >= 0) {
      numa_bitmask_clearall(mask);
      numa_bitmask_setbit(mask, numaNode);
      long ret = mbind(_dptr(i, *set), dmmDpuSize, MPOL_BIND,
                       mask->maskp, mask->size + 1, MPOL_MF_MOVE | MPOL_MF_STRICT);
      if (ret != 0)
        perror("mbind DmmDpu");
    }
  }
  numa_free_nodemask(mask);
#endif

  set->symbols = DmmMapInit(512);
  set->xfer_addr = calloc(nrDpu, sizeof(intptr_t));
  if (set->symbols == NULL || set->xfer_addr == NULL) {
    DmmMapFini(set->symbols);
    munmap(set->dmm_dpu, dmmDpuSize * nrDpu);
    return DPU_ERR_ALLOCATION;
  }
  set->begin = 0; set->end = nrDpu;
  for (size_t i = 0; i < nrDpu; ++i)
    set->dmm_dpu[i].Is = 0;
  return DPU_OK;
}
dpu_error_t dpu_alloc_ranks(uint32_t nrRank, const char *_,
                            struct dpu_set_t *set) {
  return dpu_alloc(nrRank * 64, _, set);
}

static dpu_error_t _unload(struct dpu_set_t set) {
  dpu_error_t err = DPU_OK;
  static size_t nrDump = 0;
  struct DmmDpu *firstDpu = (struct DmmDpu*)set.dmm_dpu;
  if (firstDpu->Is == UNINIT_DPUIS) return err;

  if (dumpFile != NULL) {
    size_t nthDump = nrDump++, nrInstr = 0;
    for (size_t i = 0; i < IramNrInstrR; ++i) {
      bool hasInstr = firstDpu->Is == RV_DPUIS ?
        (firstDpu->R.Program.Iram[i].Opcode != 0) :
        (firstDpu->U.Program.Iram[i].Opcode != 0);
      if (hasInstr)
        nrInstr = i;
    }
    char name[strlen(dumpFile) + 16];
    sprintf(name, dumpFile, nthDump);
    FILE *dump = fopen(name, "w");
    if (dump == NULL) return DPU_ERR_VPD_INVALID_FILE;

    for (size_t i = set.begin; i < set.end; ++i) {
      fprintf(dump, "DPU %zu\n", i);
      struct DmmDpu* d = _dptr(i, set);
      for (size_t j = 0; j < nrInstr; ++j) {
        if (d->Is == RV_DPUIS) {
          fprintf(dump, "%04zx %u %s\n", j * InstrNrByteR, d->R.Timing.StatTsc[j],
                  RvOpStr[d->R.Program.Iram[j].Opcode]);
        } else {
          fprintf(dump, "%04zx %u %s\n", j * IramNrByte, d->U.Timing.StatTsc[j],
                  UmmOpStr[d->U.Program.Iram[j].Opcode]);
        }
      }
      fputc('\n', dump);
    }
    fclose(dump);
  }

  size_t nrExec = 0, nrCycle = 0, bdRun = 0, bdDma = 0, bdRf = 0, bdPipe = 0;
  for (size_t i = set.begin; i < set.end; ++i) {
    struct DmmDpu *dpu_ = _dptr(i, set);
    if (dpu_->Is == RV_DPUIS) {
      nrExec += dpu_->R.Timing.StatNrInstrExec;
      nrCycle += dpu_->R.Timing.StatNrCycle;
      bdRun += dpu_->R.Timing.StatRun;
      bdDma += dpu_->R.Timing.StatDma;
      bdPipe += dpu_->R.Timing.StatEtc;
      bdRf += dpu_->R.Timing.StatNrRfHazard;
      RvDpuFini(&dpu_->R);
    } else {
      nrExec += dpu_->U.Timing.StatNrInstrExec;
      nrCycle += dpu_->U.Timing.StatNrCycle;
      bdRun += dpu_->U.Timing.StatRun;
      bdDma += dpu_->U.Timing.StatDma;
      bdPipe += dpu_->U.Timing.StatEtc;
      bdRf += dpu_->U.Timing.StatNrRfHazard;
      UmmDpuFini(&dpu_->U);
    }
  }
  printf("%zu %zu\n%zu %zu %zu %zu\n", nrExec, nrCycle, bdRun, bdDma, bdPipe, bdRf);
  return DPU_OK;
}

dpu_error_t dpu_free(struct dpu_set_t set) {
  dpu_error_t err = _unload(set);
  DmmMapFini(set.symbols);
  free(set.xfer_addr);
  munmap(set.dmm_dpu, dmmDpuSize * (set.end - set.begin));
  return err;
}

// don't use this dirty trick...
// _Static_assert(offsetof(struct DmmDpu, r.Program.WMAram) ==
//                offsetof(struct DmmDpu, u.Program.WMAram),
//                "WMAram dereference breaks!");

dpu_error_t dpu_load(struct dpu_set_t set, const char *objdmpPath, void **_) {
  _unload(set);
  DmmMapClear(set.symbols);
  bool paged[WMAINrPage];
  UmmPrg uprg = {NULL, NULL}; RvPrg rprg = {NULL, NULL};
  size_t nrInstr = UmmPrgLoadBinary(&uprg, objdmpPath, set.symbols, paged);
  uint8_t *prgWma = uprg.WMAram;
  if (nrInstr == 0) {
    if ((nrInstr = RvPrgLoadBinary(&rprg, objdmpPath, set.symbols, paged)) == 0)
      return DPU_ERR_ELF_INVALID_FILE;
    prgWma = rprg.WMAram;
  }

  #pragma omp parallel num_threads(nrCore)
  {
    size_t dpuId = omp_get_thread_num();
#ifdef __DMM_NUMA
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(dpuId, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0)
      perror("sched_setaffinity");
#endif
    while (dpuId < set.begin)
      dpuId += nrCore;
    while (dpuId < set.end) {
      DmmDpu *dpu = _dptr(dpuId, set);
      size_t coreId = dpuId % nrCore;
#ifdef __DMM_NUMA
      int numaNode = numa_node_of_cpu(coreId);
#else
      int numaNode = -1;
#endif
      if (prgWma == rprg.WMAram) {
        RvDpuInit(&dpu->R, memFreq, logicFreq, numaNode);
        dpu->Is = RV_DPUIS;
        uint8_t *dpuWma = dpu->R.Program.WMAram;
        for (size_t i = 0; i < WMAINrPageR; ++i)
          if (paged[i])
            memcpy(&dpuWma[i*4096], &prgWma[i * 4096], 4096);
        for (size_t i = 0; i < nrInstr; ++i)
          dpu->R.Program.Iram[i] = rprg.Iram[i];
      } else {
        UmmDpuInit(&dpu->U, memFreq, logicFreq, numaNode);
        dpu->Is = UMM_DPUIS;
        uint8_t *dpuWma = dpu->U.Program.WMAram;
        for (size_t i = 0; i < WMAINrPage; ++i)
          if (paged[i])
            memcpy(&dpuWma[i*4096], &prgWma[i * 4096], 4096);
        for (size_t i = 0; i < nrInstr; ++i)
          dpu->U.Program.Iram[i] = uprg.Iram[i];
      }
      dpuId += nrCore;
    }
  }

  UmmPrgFini(&uprg); RvPrgFini(&rprg);
  return DPU_OK;
}

dpu_error_t dpu_launch(struct dpu_set_t set, dpu_launch_policy_t _) {
  if (set.dmm_dpu[set.begin].Is == UNINIT_DPUIS)
    return DPU_ERR_NO_PROGRAM_LOADED;
  #pragma omp parallel num_threads(nrCore)
  {
    size_t dpuId = omp_get_thread_num();
#ifdef __DMM_NUMA
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(dpuId, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0)
      perror("sched_setaffinity");
#endif

    size_t nrTl = DmmMapFetch(set.symbols, "NR_TASKLETS", 11);
    if (nrTl == MapNoInt) nrTl = 1;
    while (dpuId < set.begin)
      dpuId += nrCore;
    while (dpuId < set.end) {
      struct DmmDpu *dpu = _dptr(dpuId, set);
      if (dpu->Is == RV_DPUIS) {
        RvDpuRun(&dpu->R, nrTl);
      } else {
        UmmDpuRun(&dpu->U, nrTl);
      }
      dpuId += nrCore;
    }
  }
  return DPU_OK;
}

dpu_error_t dpu_prepare_xfer(struct dpu_set_t set, void *hostAddr) {
  dpu_error_t ret = DPU_OK;
  struct dpu_set_t dpu;
  DPU_FOREACH(set, dpu) {
    if (set.xfer_addr[dpu.begin] != NULL)
      ret = DPU_ERR_TRANSFER_ALREADY_SET;
    set.xfer_addr[dpu.begin] = hostAddr;
  }
  return ret;
}

dpu_error_t
dpu_push_xfer(struct dpu_set_t set, dpu_xfer_t xfer, const char *symName,
              uint32_t symOff, size_t length, dpu_xfer_flags_t flag) {
  DmmSymAddr dAddr = DmmMapFetch(set.symbols, symName, strlen(symName)) ;
  if (dAddr == MapNoInt) return DPU_ERR_UNKNOWN_SYMBOL;
#ifndef __DMM_NOXFER
  // Estimate overhead
  long ty = 4 * (dAddr < MramBegin) + (xfer & DPU_XFER_FROM_DPU);
  _Static_assert(DPU_XFER_FROM_DPU == 1, "DPU_XFER_FROM_DPU == 1");
  printf("%s %s %zudpu %zubytes %luusec\n", ty & 4 ? "WRAM" : "MRAM",
         ty & 1 ? "dToH" : "hToD", set.end - set.begin, length,
         DmmXferOverhead(set.end - set.begin, set.xfer_addr, length, ty));
#endif
  if (dAddr >= MramBeginR)
    dAddr = WramSize + dAddr - MramBeginR;
  dAddr += symOff;

  // Single theaded xfer for small sizes
  if (length * (set.end - set.begin) <= 65536) {
    for (size_t i = set.begin; i < set.end; ++i) {
      if (set.xfer_addr[i] == NULL)
        continue;
      struct DmmDpu *dpu = _dptr(i, set);
      uint8_t *dpuWma =
          dpu->Is == RV_DPUIS ? dpu->R.Program.WMAram : dpu->U.Program.WMAram;
      if (xfer == DPU_XFER_TO_DPU)
        memcpy(&dpuWma[dAddr], set.xfer_addr[i], length);
      else
        memcpy(set.xfer_addr[i], &dpuWma[dAddr], length);
      if (!(flag & DPU_XFER_NO_RESET))
        set.xfer_addr[i] = NULL;
    }
    return DPU_OK;
  }

  #pragma omp parallel num_threads(nrCore)
  {
    size_t dpuId = omp_get_thread_num();
#ifdef __DMM_NUMA
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(dpuId, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0)
      perror("sched_setaffinity");
#endif
    while (dpuId < set.begin)
      dpuId += nrCore;
    while (dpuId < set.end) {
      if (set.xfer_addr[dpuId] != NULL) {
        struct DmmDpu *dpu = _dptr(dpuId, set);
      uint8_t *dpuWma =
          dpu->Is == RV_DPUIS ? dpu->R.Program.WMAram : dpu->U.Program.WMAram;
        if (xfer == DPU_XFER_TO_DPU)
          memcpy(&dpuWma[dAddr], set.xfer_addr[dpuId], length);
        else
          memcpy(set.xfer_addr[dpuId], &dpuWma[dAddr], length);
        if (!(flag & DPU_XFER_NO_RESET))
          set.xfer_addr[dpuId] = NULL;
      }
      dpuId += nrCore;
    }
  }
  return DPU_OK;
}

dpu_error_t dpu_copy_to(struct dpu_set_t set, const char *symName,
                        uint32_t symOff, const void *src, size_t length) {
  struct DmmDpu *dpu = _dptr(set.begin, set);
  DmmSymAddr dAddr = DmmMapFetch(set.symbols, symName, strlen(symName)) ;
  if (dAddr == MapNoInt) return DPU_ERR_UNKNOWN_SYMBOL;
  if (dAddr >= MramBeginR)
    dAddr = WramSize + dAddr - MramBeginR;
  dAddr += symOff;
  uint8_t *dpuWma =
      dpu->Is == RV_DPUIS ? dpu->R.Program.WMAram : dpu->U.Program.WMAram;
  memcpy(&dpuWma[dAddr], src, length);
  return DPU_OK;
}

dpu_error_t dpu_copy_from(struct dpu_set_t set, const char *symName,
                          uint32_t symOff, void *dst, size_t length) {
  struct DmmDpu *dpu = _dptr(set.begin, set);
  DmmSymAddr dAddr = DmmMapFetch(set.symbols, symName, strlen(symName)) ;
  if (dAddr == MapNoInt) return DPU_ERR_UNKNOWN_SYMBOL;
  if (dAddr >= MramBeginR)
    dAddr = WramSize + dAddr - MramBeginR;
  dAddr += symOff;
  uint8_t *dpuWma =
      dpu->Is == RV_DPUIS ? dpu->R.Program.WMAram : dpu->U.Program.WMAram;
  memcpy(dst, &dpuWma[dAddr], length);
  return DPU_OK;
}

dpu_error_t
dpu_broadcast_to(struct dpu_set_t set, const char *symName, uint32_t symOff,
                 const void *src, size_t length, dpu_xfer_flags_t flags) {
  dpu_error_t ret = DPU_OK;
  DmmSymAddr dAddr = DmmMapFetch(set.symbols, symName, strlen(symName)) ;
  if (dAddr == MapNoInt) return DPU_ERR_UNKNOWN_SYMBOL;
#ifndef __DMM_NOXFER
  // Estimate overhead
  long ty = 4 * (dAddr < MramBegin) + 2;
  printf("%s Bcst %zudpu %zubytes %luusec\n", ty & 4 ? "WRAM" : "MRAM",
         set.end - set.begin, length,
         DmmXferOverhead(set.end - set.begin, set.xfer_addr, length, ty));
#endif
  if (dAddr >= MramBeginR)
    dAddr = WramSize + dAddr - MramBeginR;
  dAddr += symOff;
  if (length <= 4096) {
    for (size_t i = set.begin; i < set.end; ++i) {
      if (set.xfer_addr[i] != NULL)
        ret = DPU_ERR_TRANSFER_ALREADY_SET;
      struct DmmDpu *dpu = _dptr(i, set);
      uint8_t *dpuWma =
          dpu->Is == RV_DPUIS ? dpu->R.Program.WMAram : dpu->U.Program.WMAram;
      memcpy(&dpuWma[dAddr], src, length);
      if (!(flags & DPU_XFER_NO_RESET))
        set.xfer_addr[i] = NULL;
    }
    return ret;
  }

  #pragma omp parallel num_threads(nrCore)
  {
    size_t dpuId = omp_get_thread_num();
#ifdef __DMM_NUMA
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(dpuId, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0)
      perror("sched_setaffinity");
#endif
    while (dpuId < set.begin)
      dpuId += nrCore;
    while (dpuId < set.end) {
      if (set.xfer_addr[dpuId] != NULL)
        ret = DPU_ERR_TRANSFER_ALREADY_SET;
      struct DmmDpu *dpu = _dptr(dpuId, set);
      uint8_t *dpuWma =
          dpu->Is == RV_DPUIS ? dpu->R.Program.WMAram : dpu->U.Program.WMAram;
      memcpy(&dpuWma[dAddr], src, length);
      if (!(flags & DPU_XFER_NO_RESET))
        set.xfer_addr[dpuId] = NULL;
      dpuId += nrCore;
    }
  }
  return DPU_OK;
}

// `DPU_ERR` prefix is stripped. An "A-" is prepended and hex value is appended.
static const char *errs[] = {
  "A-ERRORS UPON ERRORS, THE PROGRAM IS BROKEN BEYOND RECOGNITION",
  "A-OK 0x0", "A-INTERNAL 0x1",
  "A-SYSTEM 0x2", "A-DRIVER 0x3",
  "A-ALLOCATION 0x4", "A-INVALID_DPU_SET 0x5",
  "A-INVALID_THREAD_ID 0x6", "A-INVALID_NOTIFY_ID 0x7",
  "A-INVALID_WRAM_ACCESS 0x8", "A-INVALID_IRAM_ACCESS 0x9",
  "A-INVALID_MRAM_ACCESS 0xa", "A-INVALID_SYMBOL_ACCESS 0xb",
  "A-MRAM_BUSY 0xc", "A-TRANSFER_ALREADY_SET 0xd",
  "A-INVALID_PARALLEL_MEMORY_TRANSFER 0xe", "A-WRAM_FIFO_FULL 0xf",
  "A-NO_PROGRAM_LOADED 0x10", "A-DIFFERENT_DPU_PROGRAMS 0x11",
  "A-CORRUPTED_MEMORY 0x12", "A-DPU_DISABLED 0x13",
  "A-DPU_ALREADY_RUNNING 0x14", "A-INVALID_MEMORY_TRANSFER 0x15",
  "A-INVALID_LAUNCH_POLICY 0x16", "A-DPU_FAULT 0x17",
  "A-ELF_INVALID_FILE 0x18", "A-ELF_NO_SUCH_FILE 0x19",
  "A-ELF_NO_SUCH_SECTION 0x1a", "A-TIMEOUT 0x1b",
  "A-INVALID_PROFILE 0x1c", "A-UNKNOWN_SYMBOL 0x1d",
  "A-LOG_FORMAT 0x1e", "A-LOG_CONTEXT_MISSING 0x1f",
  "A-LOG_BUFFER_TOO_SMALL 0x20", "A-VPD_INVALID_FILE 0x21",
  "A-VPD_NO_REPAIR 0x22", "A-NO_THREAD_PER_RANK 0x23",
  "A-INVALID_BUFFER_SIZE 0x24", "A-NONBLOCKING_SYNC_CALLBACK 0x25",
  "A-TOO_MANY_TASKLETS 0x26", "A-SG_TOO_MANY_BLOCKS 0x27",
  "A-SG_LENGTH_MISMATCH 0x28", "A-SG_NOT_ACTIVATED 0x29",
  "A-SG_NOT_MRAM_SYMBOL 0x2a",
};
// Why the f* does UPMEM decide to return char* rather than const char*
char *dpu_error_to_string(dpu_error_t status) {
  bool a = status & DPU_ERR_ASYNC_JOBS;
  status &= ~DPU_ERR_ASYNC_JOBS;
  ++status;
  if (status >= 0x2b) status = 0;
  char *ret = strdup(errs[status] + (a ? 0 : 2));
  return ret;
}

static void __attribute__((constructor)) a() {
  const char* e = getenv("DMM_NR_SIM_THRDS");
  if (e != NULL) nrCore = strtoul(e, NULL, 0);
  e = getenv("DMM_LogicFrequency");
  if (e != NULL) logicFreq = strtoul(e, NULL, 0);
  e = getenv("DMM_MemoryFrequency");
  if (e != NULL) memFreq = strtoul(e, NULL, 0);
  e = getenv("DMM_TscDumpFmt");
  if (e != NULL) dumpFile = strdup(e);

  if (nrCore <= 0 || nrCore > 512) nrCore = sysconf(_SC_NPROCESSORS_ONLN);
  if (logicFreq <= 0) logicFreq = 350;
  if (memFreq <= 0) memFreq = 2400;

  dmmDpuSize = sysconf(_SC_PAGESIZE);
  while (dmmDpuSize < sizeof(struct DmmDpu))
    dmmDpuSize += 4096;
}
