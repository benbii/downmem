#define _GNU_SOURCE
#ifdef __DMM_NUMA
#include <numa.h>
#include <numaif.h>
#include <sched.h>
#endif

#include "downmem.h"
#include "dpu.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static size_t nrCore;
static size_t nrXferCore;
static size_t dmmDpuSize;
static size_t logicFreq;
static size_t memFreq;
static char* dumpFile;

static void __attribute__((constructor)) a() {
  const char* e = getenv("DMM_NR_SIM_THRDS");
  if (e != NULL) nrCore = strtoul(e, NULL, 0);
  e = getenv("DMM_NR_XFER_THRDS");
  if (e != NULL) nrXferCore = strtoul(e, NULL, 0);
  e = getenv("DMM_LogicFrequency");
  if (e != NULL) logicFreq = strtoul(e, NULL, 0);
  e = getenv("DMM_MemoryFrequency");
  if (e != NULL) memFreq = strtoul(e, NULL, 0);
  e = getenv("DMM_TscDumpFmt");
  if (e != NULL) dumpFile = strdup(e);

  if (nrCore <= 0 || nrCore > 512) nrCore = sysconf(_SC_NPROCESSORS_ONLN);
  if (nrXferCore <= 0 || nrXferCore > 512) nrXferCore = 8;
  if (logicFreq <= 0) logicFreq = 350;
  if (memFreq <= 0) memFreq = 2400;

  dmmDpuSize = sysconf(_SC_PAGESIZE);
  while (dmmDpuSize < sizeof(DmmDpu))
    dmmDpuSize += 4096;
}
static inline DmmDpu* _dptr(size_t i, struct dpu_set_t s) {
  return (DmmDpu*)((uintptr_t)s.dmm_dpu + dmmDpuSize * i);
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
  set->symbols = DmmMapInit(512);
  set->xfer_addr = calloc(nrDpu, sizeof(intptr_t));
  if (set->symbols == NULL || set->xfer_addr == NULL) {
    DmmMapFini(set->symbols);
    munmap(set->dmm_dpu, dmmDpuSize * nrDpu);
    return DPU_ERR_ALLOCATION;
  }
  set->begin = 0; set->end = nrDpu;

  #pragma omp parallel num_threads(nrCore)
  {
    const size_t coreId = omp_get_thread_num();
#ifdef __DMM_NUMA
    const size_t numaMsk = 1ULL << numa_node_of_cpu(coreId);
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(coreId, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0)
      perror("sched_setaffinity");
#endif

    for (size_t dId = coreId; dId < nrDpu; dId += nrCore) {
      DmmDpu *dmmdpu = _dptr(dId, *set);
      DmmDpuInit(dmmdpu, memFreq, logicFreq);
      // bind the DPU structure (containing things like register files) and its
#ifdef __DMM_NUMA
      // WRAM MRAM to the corresponding NUMA node
      if (mbind(dmmdpu, DmmDpuSize, MPOL_BIND, &numaMsk, sizeof(size_t), 0) ||
          mbind(dmmdpu->Program.WMAram, WMANrByte, MPOL_BIND, &numaMsk,
                sizeof(size_t), 0))
        continue;
#endif
    }
  }
  return DPU_OK;
}

dpu_error_t dpu_alloc_ranks(uint32_t nrRank, const char *_,
                            struct dpu_set_t *set) {
  return dpu_alloc(nrRank * 64, _, set);
}

dpu_error_t dpu_free(struct dpu_set_t set) {
  dpu_error_t err = DPU_OK;
  static size_t nrDump = 0;
  if (dumpFile != NULL) {
    size_t nthDump = nrDump++, nrInstr = 0;
    for (size_t i = 0; i < IramNrInstr; ++i)
      if (set.dmm_dpu->Program.Iram[i].Opcode != 0)
        nrInstr = i;
    char name[strlen(dumpFile) + 16];
    sprintf(name, dumpFile, nthDump);
    FILE *dump = fopen(name, "w");
    if (dump == NULL) { err = DPU_ERR_VPD_INVALID_FILE; goto cleanup; }

    for (size_t i = set.begin; i < set.end; ++i) {
      fprintf(dump, "DPU %zu\n", i);
      for (size_t j = 0; j < nrInstr; ++j) {
        DmmDpu* d = _dptr(i, set);
        fprintf(dump, "%04zx %u %s\n", j * 8, d->Timing.StatTsc[j],
                DmmOpStr[d->Program.Iram[j].Opcode]);
      }
      fputc('\n', dump);
    }
    fclose(dump);
  }

  size_t nrExec = 0, nrCycle = 0, bdRun = 0, bdDma = 0, bdRf = 0, bdPipe = 0;
  for (size_t i = set.begin; i < set.end; ++i) {
    DmmDpu *dpu_ = _dptr(i, set);
    nrExec += dpu_->Timing.StatNrInstrExec;
    nrCycle += dpu_->Timing.StatNrCycle;
    bdRun += dpu_->Timing.StatRun;
    bdDma += dpu_->Timing.StatDma;
    bdPipe += dpu_->Timing.StatEtc;
    bdRf += dpu_->Timing.StatNrRfHazard;
    DmmDpuFini(dpu_);
  }
  printf("%zu %zu\n%zu %zu %zu %zu\n", nrExec, nrCycle, bdRun, bdDma, bdPipe, bdRf);

cleanup:
  DmmMapFini(set.symbols);
  free(set.xfer_addr);
  munmap(set.dmm_dpu, dmmDpuSize * (set.end - set.begin));
  return err;
}

dpu_error_t dpu_load(struct dpu_set_t set, const char *objdmpPath, void **_) {
  bool paged[WMAINrPage];
  DmmPrg prg; DmmPrgInit(&prg);
  size_t nrInstr = DmmPrgReadObjdump(&prg, objdmpPath, set.symbols, paged);
  if (nrInstr == 0)
    return DPU_ERR_ELF_INVALID_FILE;
  uint64_t *prgWma = (uint64_t*)prg.WMAram;

  #pragma omp parallel num_threads(nrXferCore)
  {
    size_t dpuId = omp_get_thread_num();
#ifdef __DMM_NUMA
    // the n-th OMP thread binds to the n-th CPU core and processes the
    // n, n+nrCore, ... DPU
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(dpuId, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0)
      perror("sched_setaffinity");
#endif

    while (dpuId < set.begin)
      dpuId += nrCore;
    while (dpuId < set.end) {
      DmmDpu *dpu = _dptr(dpuId, set);
      uint64_t *dpuWma = (uint64_t*)dpu->Program.WMAram;
      for (size_t i = 0; i < WMAINrPage; ++i)
        if (paged[i])
          memcpy(&dpuWma[i*4096], &prgWma[i * 4096], 4096);
      for (size_t i = 0; i < nrInstr; ++i)
        dpu->Program.Iram[i] = prg.Iram[i];
      dpuId += nrXferCore;
    }
  }

  DmmPrgFini(&prg);
  return DPU_OK;
}

dpu_error_t dpu_launch(struct dpu_set_t set, dpu_launch_policy_t _) {
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
      DmmDpu *dpu = _dptr(dpuId, set);
      DmmDpuRun(dpu, nrTl);
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
  if (dAddr >= MramBegin)
    dAddr = WramSize + dAddr - MramBegin;
  dAddr += symOff;

  // Single theaded xfer for small sizes
  if (length * (set.end - set.begin) <= 65536) {
    for (size_t i = set.begin; i < set.end; ++i) {
      if (set.xfer_addr[i] == NULL)
        continue;
      DmmDpu *dpu = _dptr(i, set);
      if (xfer == DPU_XFER_TO_DPU)
        memcpy(&dpu->Program.WMAram[dAddr], set.xfer_addr[i], length);
      else
        memcpy(set.xfer_addr[i], &dpu->Program.WMAram[dAddr], length);
      if (!(flag & DPU_XFER_NO_RESET))
        set.xfer_addr[i] = NULL;
    }
    return DPU_OK;
  }

  #pragma omp parallel num_threads(nrXferCore)
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
        DmmDpu *dpu = _dptr(dpuId, set);
        if (xfer == DPU_XFER_TO_DPU)
          memcpy(&dpu->Program.WMAram[dAddr], set.xfer_addr[dpuId], length);
        else
          memcpy(set.xfer_addr[dpuId], &dpu->Program.WMAram[dAddr], length);
        if (!(flag & DPU_XFER_NO_RESET))
          set.xfer_addr[dpuId] = NULL;
      }
      dpuId += nrXferCore;
    }
  }
  return DPU_OK;
}

dpu_error_t dpu_copy_to(struct dpu_set_t set, const char *symName,
                        uint32_t symOff, const void *src, size_t length) {
  DmmDpu *dpu = _dptr(set.begin, set);
  DmmSymAddr dAddr = DmmMapFetch(set.symbols, symName, strlen(symName)) ;
  if (dAddr == MapNoInt) return DPU_ERR_UNKNOWN_SYMBOL;
  if (dAddr >= MramBegin)
    dAddr = WramSize + dAddr - MramBegin;
  dAddr += symOff;
  memcpy(&dpu->Program.WMAram[dAddr], src, length);
  return DPU_OK;
}

dpu_error_t dpu_copy_from(struct dpu_set_t set, const char *symName,
                          uint32_t symOff, void *dst, size_t length) {
  DmmDpu *dpu = _dptr(set.begin, set);
  DmmSymAddr dAddr = DmmMapFetch(set.symbols, symName, strlen(symName)) ;
  if (dAddr == MapNoInt) return DPU_ERR_UNKNOWN_SYMBOL;
  if (dAddr >= MramBegin)
    dAddr = WramSize + dAddr - MramBegin;
  dAddr += symOff;
  memcpy(dst, &dpu->Program.WMAram[dAddr], length);
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
  if (dAddr >= MramBegin)
    dAddr = WramSize + dAddr - MramBegin;
  dAddr += symOff;
  if (length <= 4096) {
    for (size_t i = set.begin; i < set.end; ++i) {
      if (set.xfer_addr[i] != NULL)
        ret = DPU_ERR_TRANSFER_ALREADY_SET;
      DmmDpu *dpu = _dptr(i, set);
      memcpy(&dpu->Program.WMAram[dAddr], src, length);
      if (!(flags & DPU_XFER_NO_RESET))
        set.xfer_addr[i] = NULL;
    }
    return ret;
  }

  #pragma omp parallel num_threads(nrXferCore)
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
      DmmDpu *dpu = _dptr(dpuId, set);
      memcpy(&dpu->Program.WMAram[dAddr], src, length);
      if (!(flags & DPU_XFER_NO_RESET))
        set.xfer_addr[dpuId] = NULL;
      dpuId += nrXferCore;
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
  if (status >= 0x2b) status = 0x2b;
  char *ret = strdup(errs[status] + (a ? 2 : 0));
  if (ret == NULL) exit(1);
  if (a) ret += 2;
  return ret;
}

