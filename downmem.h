#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef __DMM_NOXFER
// Estimates the overhead of a given transfer.
// The exact mechanism depends on CMake flags when compiling Dmm.
// Returns time in microseconds. `xferAddrs` not used unless using analytical
// simulation.
// ty & 1: 1 device to host, 0 host to device
// ty & 2: 1 broadcast, 0 not broadcast
// ty & 4: 1 wram, 0 mram
uint64_t DmmXferOverhead(size_t nrDpu, void *xferAddrs[], uint64_t xferSz, long ty);
#endif

typedef void* DmmMap;
DmmMap DmmMapInit(size_t initCap);
void DmmMapAssign(DmmMap map, const void *str, size_t sz, uint_fast32_t val);
uint_fast32_t DmmMapFetch(DmmMap map, const void* str, size_t sz);
void DmmMapFini(DmmMap map);
enum { MapNoInt = 0x44f8a1ef, noAddr = 0x44f8a1ef };

// --- Constant Definitions ---
enum {
  AtomicMask     = 0x000000ff,
  AtomicSize     = 256,
  IramDataByte   = 8,
  IramMask       = 0x7fffffff,
  IramNrInstr    = 4096,
  IramSize       = IramDataByte * IramNrInstr,
  NrPipelineStage= 14,
  NrRevolveCycle = 11,
  WramSize       = 65536,
  WramMask       = 65535,
  MramMask       = 0x07fffff8,
  MramBegin      = 0x08000000,
  MramSize       = 64 * 1024 * 1024,
  NumGpRegisters = 24,
  MaxNumTasklets = 24,

  ZeroReg = NumGpRegisters,
  NullReg = NumGpRegisters + 8,
  ZeroImm = 0,
  badReg  = 99,
  badImm  = MapNoInt,

  WordlineSz = 1024,
  ReorderWinSz = 256,
  TRp = 32,
  TRcd = 32,
  TRas = 78,
  TCl = 32,
  TBl = 8
};
typedef size_t DmmSymAddr;

// --- Enums for conditions and opcodes ---
typedef enum {
  NoCond,
  TRUE, FALSE, Z, NZ, SZ, SNZ, PL, MI, SPL, SMI, V, NV, C, NC, LTU, GEU, LEU,
  GTU, LTS, GES, LES, GTS, EQ, NEQ, XZ, NXZ, XLEU, XGTU, XLES, XGTS,
  SE, SO, NSH32, SH32, MAX, NMAX, SMALL, LARGE,
  NrConds,
  badCond = 0xdead
} DmmCc;

typedef enum {
  LDMA, LDMAI, SDMA, MOVE, LW, LBS, LBU, LHS, LHU, SB, SH, SW, SB_ID,
  SH_ID, SW_ID, SD_ID, EXTSB, EXTSH, MUL_SL_SL, MUL_SL_SH, MUL_SH_SL,
  MUL_SH_SH, MUL_SL_UL, MUL_SL_UH, MUL_SH_UL, MUL_SH_UH, MUL_UL_SL, MUL_UL_SH,
  MUL_UH_SL, MUL_UH_SH, EXTUB, EXTUH, CLZ, CLO, CLS, CAO, MUL_UL_UL, MUL_UL_UH,
  MUL_UH_UL, MUL_UH_UH, JMP, CALL, ACQUIRE, RELEASE, STOP, BOOT, RESUME,
  CLR_RUN, TIME, TIME_CFG, NOP, FAULT, ADD, ADDC, SUB, SUBC, AND, NAND, ANDN,
  OR, NOR, ORN, XOR, NXOR, HASH, SATS, CMPB4, ROL, ROR, LSL, LSL1, LSLX, LSL1X,
  ASR, LSR, LSR1, LSRX, LSR1X, ROL_ADD, LSR_ADD, LSL_ADD, LSL_SUB, NEG, NOT,
  SD, MUL_STEP, DIV_STEP, MOVD, SWAPD, LD,

  MOVE_S, LBS_S, LHS_S, EXTSB_S, EXTSH_S, MUL_SL_SL_S, MUL_SL_SH_S,
  MUL_SH_SL_S, MUL_SH_SH_S, MUL_SL_UL_S, MUL_SL_UH_S, MUL_SH_UL_S, MUL_SH_UH_S,
  MUL_UL_SL_S, MUL_UL_SH_S, MUL_UH_SL_S, MUL_UH_SH_S, ADD_S, ADDC_S, SUB_S,
  SUBC_S, AND_S, NAND_S, ANDN_S, OR_S, NOR_S, ORN_S, XOR_S, NXOR_S, LW_S,
  HASH_S, SATS_S, CMPB4_S, ROL_S, ROR_S, LSL_S, LSL1_S, LSLX_S, LSL1X_S, ASR_S,
  LSR_S, LSR1_S, LSRX_S, LSR1X_S, ROL_ADD_S, LSR_ADD_S, LSL_ADD_S, LSL_SUB_S,

  MOVE_U, LBU_U, LHU_U, EXTUB_U, EXTUH_U, CLZ_U, CLO_U, CLS_U, CAO_U,
  MUL_UL_UL_U, MUL_UL_UH_U, MUL_UH_UL_U, MUL_UH_UH_U, ADD_U, ADDC_U, SUB_U,
  SUBC_U, AND_U, NAND_U, ANDN_U, OR_U, NOR_U, ORN_U, XOR_U, NXOR_U, LW_U,
  HASH_U, SATS_U, CMPB4_U, ROL_U, ROR_U, LSL_U, LSL1_U, LSLX_U, LSL1X_U, ASR_U,
  LSR_U, LSR1_U, LSRX_U, LSR1X_U, ROL_ADD_U, LSR_ADD_U, LSL_ADD_U, LSL_SUB_U,

  NrOpcode,
  SOpcodeStart = MOVE_S,
  UOpcodeStart = MOVE_U,
  // noOpcode     = MapNoInt
} DmmOpcode;

enum DmmOpWbMode {
  noWb, // No writeback
  wbNoZf, // write result back but keep ZeroFlag unchanged
  wbZf, // update ZeroFlag write result back
  wbNoZf_s,
  wbZf_s,
  wbNoZf_u,
  wbZf_u,
  wbShAdd, // used by ls?_add
  wbShAdd_s,
  wbShAdd_u,
};

// --- Lookup Table Declarations ---
extern uint8_t DmmOpWbMode[NrOpcode];
extern const char *DmmOpStr[NrOpcode], *DmmOpStr[NrOpcode], *DmmCcStr[NrConds];
extern DmmMap DmmStrToOpcode, DmmStrToCc, DmmStrToJcc;

// --- Instruction Struct ---
typedef struct DmmInstr {
  DmmOpcode Opcode;
  DmmCc Cond;
  size_t RegA, RegB, RegC;
  size_t ImmA, ImmB;
} DmmInstr;

// --- Program Struct and Member Functions ---
// TODO: typedef void* DmmPrg
typedef struct DmmPrg {
  uint8_t* WMAram;
  DmmInstr* Iram;
} DmmPrg;
enum {
  _WMAINrByte = WramSize + MramSize + AtomicSize + IramNrInstr * sizeof(DmmInstr),
  WMAINrPage = (_WMAINrByte + 4095) / 4096,
  WMAINrByte = WMAINrPage * 4096
};
void DmmPrgInit(DmmPrg* p);
void DmmPrgFini(DmmPrg* p);
size_t DmmPrgReadObjdump(DmmPrg *p, const char *filename, DmmMap symbols,
                         bool paged[WMAINrPage]);

// --- Tasklet ---
typedef enum { SLEEP, RUNNABLE, BLOCK } DmmTletState;
typedef struct {
  size_t Id;
  uint8_t CarryFlag;
  DmmTletState State;
  size_t Pc;
  uint32_t Regs[NumGpRegisters + 10];
} DmmTlet;
void DmmTletInit(DmmTlet* tl, uint8_t id);

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

// --- Top-level Timing Struct ---
typedef struct Timing {
  DmmTlet Threads[MaxNumTasklets];
  DmmInstr *Iram;
  double FreqRatio;
  DmmMramTiming MramTiming;

  // Variables to track which threads and instructions are in pipeline
  DmmInstr* PpInInstr;
  long PpInId;
  // instrs inside the pipeline, maintained using a queue to avoid moving
  DmmInstr* PpInsideInstrs[16];
  uint_fast8_t PpInsideIds[16];
  uint_fast8_t PpQFrt;
  uint_fast8_t PpQRear;
  // instr that completed the pipeline to be reaped
  DmmInstr* PpReadyInstr;
  long PpReadyId;

  // Fields to enforce register binning rules
  DmmInstr* CrCurInstr;
  DmmInstr* CrPrevInstr;
  long CrCurId;
  long CrPrevId;
  long CrExtraCycleLeft;
  uint32_t CrPrevWriteRegSets[MaxNumTasklets];

  long StatNrCycle;
  long StatNrInstrExec;
  long StatNrRfHazard;
  long StatRun;
  long StatDma;
  long StatEtc;
  long StatCycleRule;

  // used to track each instructions' tsc (cycles each instruction takes)
  long lastIssue;
  long lastRunAt[MaxNumTasklets];
  size_t lastPc[MaxNumTasklets];
  uint32_t StatTsc[IramNrInstr];
} DmmTiming;

void DmmTimingInit(DmmTiming *t, DmmInstr *iram, size_t memFreq, size_t logicFreq);
// Discard the instruction return value
DmmTlet* DmmTimingCycle(DmmTiming *t, size_t nrTasklets);
static inline void DmmTimingFini(DmmTiming* t) {
  DmmMramTimingFini(&t->MramTiming);
}

// --- DMM Simulated DPU Interface ---
typedef struct DmmDpu {
  DmmPrg Program;
  DmmTiming Timing;
} DmmDpu;
void DmmDpuInit(DmmDpu* d, size_t memFreq, size_t logicFreq);
void DmmDpuRun(DmmDpu* d, size_t nrTasklets);
void DmmDpuExecuteInstr(DmmDpu* d, DmmTlet* thread);
static inline void DmmDpuFini(DmmDpu* d) {
  DmmPrgFini(&d->Program);
  DmmTimingFini(&d->Timing);
}

// --- Objdump Parsing Functions ---
typedef struct {
    uint32_t NrDat;
    uint32_t Addr;
    uint32_t Dat[4];
} ObjdLnToDatRet;
// ObjdLnToSym tries parsing an objdump line of size sz into a data structure.
ObjdLnToDatRet ObjdLnToDat(const char* objdumpLine, size_t sz);
// ObjdLnToSym tries parsing an objdump line of size *outBufSz into a (symbol
// name, SymbAddr) pair. SymbAddr is returned, and symbol name is saved into
// outBuf. If the line does not match, it sets *outBufSz to 0.
DmmSymAddr ObjdLnToSym(const char *objdumpLine, size_t linesz, char *outBuf,
                       size_t* outBufSz);
