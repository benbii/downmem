#ifndef __DMM_H
#define __DMM_H
#include "../dmm_common.h"

// --- UPMEM ISA Specific Constants ---
enum {
  AtomicMask     = 0x000000ff,
  AtomicSize     = 256,
  IramNrByte   = 8,
  IramMask       = 0x7fffffff,
  IramNrInstr    = 4096,
  IramSize       = IramNrByte * IramNrInstr,
  WramSize       = 65536,
  WramMask       = 65535,
  MramMask       = 0x07fffff8,
  MramBegin      = 0x08000000,
  MramSize       = 64 * 1024 * 1024,
  NumGpRegisters = 24,

  ZeroReg = NumGpRegisters,
  NullReg = NumGpRegisters + 8,
  ZeroImm = 0,
  badReg  = 99,
  badImm  = MapNoInt
};

// --- Enums for conditions and opcodes ---
typedef enum {
  NoCond,
  TRUE, FALSE, Z, NZ, SZ, SNZ, PL, MI, SPL, SMI, V, NV, C, NC, LTU, GEU, LEU,
  GTU, LTS, GES, LES, GTS, EQ, NEQ, XZ, NXZ, XLEU, XGTU, XLES, XGTS,
  SE, SO, NSH32, SH32, MAX, NMAX, SMALL, LARGE,
  NrConds,
  badCond = 0xdead
} UmmCc;

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
  UOpcodeStart = MOVE_U
} UmmOpcode;

enum UmmOpWbMode {
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
extern uint8_t UmmOpWbMode[NrOpcode];
extern const char *UmmOpStr[NrOpcode], *UmmOpStr[NrOpcode], *UmmCcStr[NrConds];
extern DmmMap UmmStrToOpcode, UmmStrToCc, UmmStrToJcc;

// --- Instruction Struct ---
typedef struct UmmInstr {
  UmmOpcode Opcode;
  UmmCc Cond;
  size_t RegA, RegB, RegC;
  size_t ImmA, ImmB;
} UmmInstr;

// --- Program Struct and Member Functions ---
typedef struct UmmPrg {
  uint8_t* WMAram;
  UmmInstr* Iram;
} UmmPrg;
enum {
  _WMAINrByte = WramSize + MramSize + AtomicSize + IramNrInstr * sizeof(UmmInstr),
  WMAINrPage = (_WMAINrByte + 4095) / 4096,
  WMAINrByte = WMAINrPage * 4096
};
void UmmPrgInit(UmmPrg* p, int numaNode);
void UmmPrgFini(UmmPrg* p);
size_t UmmPrgLoadBinary(UmmPrg *p, const char *filename, DmmMap symbols,
                         bool paged[WMAINrPage]);

// --- Tasklet ---
typedef enum { SLEEP, RUNNABLE, BLOCK } UmmTletState;
typedef struct {
  size_t Id;
  uint8_t CarryFlag;
  UmmTletState State;
  size_t Pc;
  uint32_t Regs[NumGpRegisters + 10];
} UmmTlet;
void UmmTletInit(UmmTlet* tl, uint8_t id);

// --- Top-level Timing Struct ---
typedef struct Timing {
  UmmTlet Threads[MaxNumTasklets];
  UmmInstr *Iram;
  double FreqRatio;
  DmmMramTiming MramTiming;

  // Variables to track which threads and instructions are in pipeline
  UmmInstr* PpInInstr;
  long PpInId;
  // instrs inside the pipeline, maintained using a queue to avoid moving
  UmmInstr* PpInsideInstrs[16];
  uint_fast8_t PpInsideIds[16];
  uint_fast8_t PpQFrt;
  uint_fast8_t PpQRear;
  // instr that completed the pipeline to be reaped
  UmmInstr* PpReadyInstr;
  long PpReadyId;

  // Fields to enforce register binning rules
  UmmInstr* CrCurInstr;
  UmmInstr* CrPrevInstr;
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
} UmmTiming;

void UmmTimingInit(UmmTiming *t, UmmInstr *iram, size_t memFreq, size_t logicFreq);
// Discard the instruction return value
UmmTlet* UmmTimingCycle(UmmTiming *t, size_t nrTasklets);
static inline void UmmTimingFini(UmmTiming* t) {
  DmmMramTimingFini(&t->MramTiming);
}

// --- DMM Simulated DPU Interface ---
typedef struct UmmDpu {
  UmmPrg Program;
  UmmTiming Timing;
} UmmDpu;
void UmmDpuInit(UmmDpu* d, size_t memFreq, size_t logicFreq, int numaNode);
void UmmDpuRun(UmmDpu* d, size_t nrTasklets);
void UmmDpuExecuteInstr(UmmDpu* d, UmmTlet* thread);
static inline void UmmDpuFini(UmmDpu* d) {
  UmmPrgFini(&d->Program);
  UmmTimingFini(&d->Timing);
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
#endif
