#ifndef __RV_DMM_H
#define __RV_DMM_H
#include "../dmm_common.h"

// --- RISC-V ISA Specific Constants ---
enum {
  // Same as UPMEM
  WramSizeR       = 65536,
  WramMaskR       = 65535,
  MramMaskR       = 0x07ffffff,
  MramBeginR      = 0x08000000,
  MramSizeR       = 64 * 1024 * 1024,
  AtomicSizeR     = 32,
  // RISC-V has 32 general-purpose registers (x0-x31)
  NumGpRegistersR = 32,
  IramBeginR      = 0x80000000,
  IramNrInstrR    = 4096,
  InstrNrByteR    = 4
};

// --- RISC-V Opcodes (compact internal representation) ---
typedef enum {
  // Arithmetic & Logic
  ADDr, SUBr, ANDr, ORr, XORr, SLL, SRL, SRA, SLT, SLTU,
  // RV32M: Multiplication & Division
  MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU,
  // RV32B Zbb: Basic bit manipulation
  MIN, MAXr, MINU, MAXU,
  CLZr, CTZ, CPOP,           // Count leading/trailing zeros, popcount
  SEXT_B, SEXT_H, ZEXT_H,   // Sign/zero extend
  ANDNr, ORNr, XNOR,          // Bitwise NOT-AND, NOT-OR, NOT-XOR
  ROLr, RORr, RORI,           // Rotate left/right
  REV8, ORC_B,              // Byte reverse, OR-combine
  // Immediate variants
  ADDI, ANDI, ORI, XORI, SLLI, SRLI, SRAI, SLTI, SLTIU,
  // Upper immediate
  LUI, AUIPC,
  // Memory operations
  LBr, LHr, LWr, LBUr, LHUr, SBr, SHr, SWr,
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
  RvNrOpcode, UNKNOWN
} RvOpcode;

// --- RISC-V Instruction Struct ---
typedef struct RvInstr {
  RvOpcode Opcode;
  uint8_t rd;  // Destination register (0-31)
  uint8_t rs1; // Source register 1 (0-31)
  uint8_t rs2; // Source register 2 (0-31)
  // Immediate value (offset in some instructions, sign extended)
  int32_t imm;
} RvInstr;

// --- Program Struct ---
typedef struct RvPrg {
  uint8_t* WMAram;         // Working memory + MRAM
  RvInstr* Iram;         // Instruction memory (binary instructions)
} RvPrg;
enum {
  _WMAINrByteR = WramSizeR + MramSizeR + IramNrInstrR * sizeof(RvInstr),
  WMAINrPageR = (_WMAINrByteR + 4095) / 4096,
  WMAINrByteR = WMAINrPageR * 4096
};
void RvPrgInit(RvPrg* p);
void RvPrgFini(RvPrg* p);
// Binary instruction loading instead of objdump parsing
size_t RvPrgLoadBinary(RvPrg *p, const char *filename, DmmMap symbols,
                        bool paged[WMAINrPageR]);

// --- RISC-V Tasklet (no explicit state field) ---
typedef struct {
  uint32_t Id;
  uint32_t Pc;                    // Program counter
  uint32_t Regs[NumGpRegistersR];  // 32 RISC-V registers (x0-x31)
} RvTlet;
void RvTletInit(RvTlet* tl, uint8_t id);

// --- RISC-V Timing Struct ---
typedef struct RvTiming {
  RvTlet Threads[MaxNumTasklets];
  RvInstr *Iram;
  double FreqRatio;
  DmmMramTiming MramTiming;
  uint32_t Csr[AtomicSizeR];    // 32 CSR words for atomic operations

  // Pipeline state (simplified for RISC-V)
  RvInstr* PpInInstr;
  long PpInId;
  RvInstr* PpInsideInstrs[16];
  uint_fast8_t PpInsideIds[16];
  uint_fast8_t PpQFrt;
  uint_fast8_t PpQRear;
  RvInstr* PpReadyInstr;
  long PpReadyId;

  // Fields to enforce register binning rules
  RvInstr* CrCurInstr;
  RvInstr* CrPrevInstr;
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
  uint32_t StatTsc[IramNrInstrR];
} RvTiming;

void RvTimingInit(RvTiming *t, RvInstr *iram, size_t memFreq,
                   size_t logicFreq);
RvTlet* RvTimingCycle(RvTiming *t, size_t nrTasklets);
static inline void RvTimingFini(RvTiming* t) {
  DmmMramTimingFini(&t->MramTiming);
}

// --- RISC-V DPU Interface ---
typedef struct RvDpu {
  RvPrg Program;
  RvTiming Timing;
} RvDpu;

void RvDpuInit(RvDpu* d, size_t memFreq, size_t logicFreq);
void RvDpuRun(RvDpu* d, size_t nrTasklets);
void RvDpuExecuteInstr(RvDpu* d, RvTlet* thread);
static inline void RvDpuFini(RvDpu* d) {
  RvPrgFini(&d->Program);
  RvTimingFini(&d->Timing);
}

// --- Lookup Tables ---
extern const char* RvOpStr[RvNrOpcode];
extern const uint8_t RvNeedRw[RvNrOpcode];
#endif
