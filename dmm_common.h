// Declarations in this file are ISA-agnostic
#pragma once
#ifdef __cplusplus
extern "C" {
#include <atomic>
#include <cstddef>
#include <cstring>
#else
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#endif

enum DmmXferTy {
  // Broadcast implies HtoD
  DmmHtoDMram = 0, DmmDtoHMram = 1, DmmBcstMram = 2,
  DmmHtoDWram = 4, DmmDtoHWram = 5, DmmBcstWram = 6,
  // ty & 1: 1 device to host, 0 host to device
  // ty & 2: 1 broadcast, 0 not broadcast
  // ty & 4: 1 wram, 0 mram
  DmmXferDtoH = 1, DmmXferBcst = 2, DmmXferWram = 4,
};

// A record of timing of a DPU launch or transfer
struct DmmDpuRecord {
  size_t NrDpu, Usec;
  union {
    size_t BdExec;
    size_t Lt7IfXferTy;
  };
  size_t BdDma, BdPipe, BdRf;
};
// fixed size; later records overwrite earlier ones
extern struct DmmDpuRecord DmmDpuRecords[2048];
#ifdef __cplusplus
extern std::atomic<size_t> NrDmmDpuRecord, DmmTotExecUsec, DmmTotXferUsec;
extern thread_local size_t DmmLastRecordIdx;
#else
extern atomic_size_t NrDmmDpuRecord, DmmTotExecUsec, DmmTotXferUsec;
extern _Thread_local size_t DmmLastRecordIdx;
#endif

#ifndef __DMM_NOXFER
// Estimates the overhead of a given transfer.
// The exact mechanism depends on CMake flags when compiling Dmm.
// Returns time in microseconds. `xferAddrs` not used unless using analytical
// simulation.
uint64_t DmmXferOverhead(size_t nrDpu, void *xferAddrs[], uint64_t xferSz,
                         enum DmmXferTy ty);
#endif

typedef void* DmmMap;
DmmMap DmmMapInit(size_t initCap);
void DmmMapAssign(DmmMap map, const void *str, size_t sz, uint_fast32_t val);
uint_fast32_t DmmMapFetch(DmmMap map, const void* str, size_t sz);
void DmmMapFini(DmmMap map);
void DmmMapClear(DmmMap map);
enum { MapNoInt = 0x44f8a1ef, noAddr = 0x44f8a1ef };
typedef size_t DmmSymAddr;

// --- Common Constants (ISA-agnostic) ---
enum {
  MaxNumTasklets = 24,
  WordlineSz = 1024,
  ReorderWinSz = 256,
  TRp = 32,
  TRcd = 32,
  TRas = 78,
  TCl = 32,
  TBl = 8,
  NrPipelineStage= 14,
  NrRevolveCycle = 11,
};

// --- Helper MRAM timing structs ---
typedef struct {
  long address;
  long thrd_id;
} _memcmd;
typedef struct {
  _memcmd* data;
  size_t capacity;
  size_t size;
  size_t offset;  // For efficient front removal
} _memcmdSlice;

typedef struct MramTiming {
  _memcmdSlice ScheRob;
  _memcmdSlice ScheReadyQ;
  long ScheRowAddr;

  long RowbufAddr;
  _memcmd RowbufInSlot;
  _memcmd RowbufIoSlot;
  _memcmd RowbufBusSlot;
  long RowbufIoSince;
  long RowbufBusSince;
  long RowbufPrechSince;

  long WaitIds[16];
  long NrWait;
  long AckLeft[MaxNumTasklets];
  long ReadyId;

  long StatMemoryCycle;
  long StatNrFr;
  long StatNrFcfs;
  long StatNrAccess;
} DmmMramTiming;

void DmmMramTimingInit(DmmMramTiming* mt);
void DmmMramTimingFini(DmmMramTiming* mt);
void DmmMramTimingPush(DmmMramTiming* mt, long begin_addr, long size, long thrd_id);
void DmmMramTimingCycle(DmmMramTiming* mt);
static inline bool DmmMramTimingCanPop(DmmMramTiming* mt) {
  return mt->ReadyId >= 0;
}
static inline long DmmMramTimingPop(DmmMramTiming* mt) {
  long val = mt->ReadyId;
  mt->ReadyId = -1;
  return val;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
