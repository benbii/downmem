#pragma once
#include "../dmm_common.h"

// --- RISC-V ISA Specific Constants ---
enum {
  // Same as UPMEM
  WramSize       = 65536,
  WramMask       = 65535,
  MramMask       = 0x07ffffff,
  MramBegin      = 0x08000000,
  MramSize       = 64 * 1024 * 1024,
  AtomicSize     = 32,
  // RISC-V has 32 general-purpose registers (x0-x31)
  NumGpRegisters = 32,
  IramBegin      = 0x80000000,
  IramNrInstr    = 4096,
  InstrNrByte    = 4
};

// --- RISC-V Opcodes (compact internal representation) ---
typedef enum {
  // Arithmetic & Logic
  ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU,
  // RV32M: Multiplication & Division
  MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU,
  // RV32B Zbb: Basic bit manipulation
  MIN, MAX, MINU, MAXU,
  CLZ, CTZ, CPOP,           // Count leading/trailing zeros, popcount
  SEXT_B, SEXT_H, ZEXT_H,   // Sign/zero extend
  ANDN, ORN, XNOR,          // Bitwise NOT-AND, NOT-OR, NOT-XOR
  ROL, ROR, RORI,           // Rotate left/right
  REV8, ORC_B,              // Byte reverse, OR-combine
  // Immediate variants
  ADDI, ANDI, ORI, XORI, SLLI, SRLI, SRAI, SLTI, SLTIU,
  // Upper immediate
  LUI, AUIPC,
  // Memory operations
  LB, LH, LW, LBU, LHU, SB, SH, SW,
  // Branches
  BEQ, BNE, BLT, BGE, BLTU, BGEU,
  // Jumps
  JAL, JALR,
  // System
  ECALL, EBREAK,
  // CSR instructions
  CSRRW, CSRRS, CSRRC, CSRRWI, CSRRSI, CSRRCI,
  // Custom DPU instructions
  MYID, LDMRAM, SDMRAM,
  // Fence (for memory ordering)
  FENCE,
  // Unknown/Invalid
  NrOpcode, UNKNOWN
} DmmRvOpcode;

// --- RISC-V Instruction Struct ---
typedef struct DmmInstr {
  DmmRvOpcode Opcode;
  uint8_t rd;  // Destination register (0-31)
  uint8_t rs1; // Source register 1 (0-31)
  uint8_t rs2; // Source register 2 (0-31)
  // Immediate value (offset in some instructions, sign extended)
  int32_t imm;
} DmmInstr;

// --- Program Struct ---
typedef struct DmmPrg {
  uint8_t* WMAram;         // Working memory + MRAM
  DmmInstr* Iram;         // Instruction memory (binary instructions)
} DmmPrg;
enum {
  _WMAINrByte = WramSize + MramSize + IramNrInstr * sizeof(DmmInstr),
  WMAINrPage = (_WMAINrByte + 4095) / 4096,
  WMAINrByte = WMAINrPage * 4096
};
void DmmPrgInit(DmmPrg* p);
void DmmPrgFini(DmmPrg* p);
// Binary instruction loading instead of objdump parsing
size_t DmmPrgLoadBinary(DmmPrg *p, const char *filename, DmmMap symbols,
                        bool paged[WMAINrPage]);

// --- RISC-V Tasklet (no explicit state field) ---
typedef struct {
  uint32_t Id;
  uint32_t Pc;                    // Program counter
  uint32_t Regs[NumGpRegisters];  // 32 RISC-V registers (x0-x31)
} DmmTlet;
void DmmTletInit(DmmTlet* tl, uint8_t id);

// --- RISC-V Timing Struct ---
typedef struct Timing {
  DmmTlet Threads[MaxNumTasklets];
  DmmInstr *Iram;
  double FreqRatio;
  DmmMramTiming MramTiming;
  uint32_t Csr[AtomicSize];    // 32 CSR words for atomic operations

  // Pipeline state (simplified for RISC-V)
  DmmInstr* PpInInstr;
  long PpInId;
  DmmInstr* PpInsideInstrs[16];
  uint_fast8_t PpInsideIds[16];
  uint_fast8_t PpQFrt;
  uint_fast8_t PpQRear;
  DmmInstr* PpReadyInstr;
  long PpReadyId;

  // Fields to enforce register binning rules
  DmmInstr* CrCurInstr;
  DmmInstr* CrPrevInstr;
  long CrCurId;
  long CrPrevId;
  long CrExtraCycleLeft;
  uint32_t CrPrevWriteRegSets[MaxNumTasklets];

  // Statistics
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

void DmmTimingInit(DmmTiming *t, DmmInstr *iram, size_t memFreq,
                   size_t logicFreq);
DmmTlet* DmmTimingCycle(DmmTiming *t, size_t nrTasklets);
static inline void DmmTimingFini(DmmTiming* t) {
  DmmMramTimingFini(&t->MramTiming);
}

// --- RISC-V DPU Interface ---
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

// --- Lookup Tables ---
extern const char* DmmOpStr[NrOpcode];
extern const uint8_t DmmRvNeedRw[NrOpcode];

