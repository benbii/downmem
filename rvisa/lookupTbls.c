#include "dmminternal.h"

// Minimal RISC-V opcode strings for host API compatibility
const char* RvOpStr[RvNrOpcode] = {
  // Arithmetic & Logic
  [ADDr] = "add", [SUBr] = "sub", [ANDr] = "and", [ORr] = "or", [XORr] = "xor",
  [SLL] = "sll", [SRL] = "srl", [SRA] = "sra", [SLT] = "slt", [SLTU] = "sltu",
  // RV32M: Multiplication & Division
  [MUL] = "mul", [MULH] = "mulh", [MULHSU] = "mulhsu", [MULHU] = "mulhu",
  [DIV] = "div", [DIVU] = "divu", [REM] = "rem", [REMU] = "remu",
  // Immediate variants
  [ADDI] = "addi", [ANDI] = "andi", [ORI] = "ori", [XORI] = "xori",
  [SLLI] = "slli", [SRLI] = "srli", [SRAI] = "srai",
  [SLTI] = "slti", [SLTIU] = "sltiu",
  // RV32B Zbb: Basic bit manipulation (R-type: read rs1+rs2, write rd)
  [MIN] = "min", [MAXr] = "max", [MINU] = "minu", [MAXU] = "maxu",
  [ANDNr] = "andn", [ORNr] = "orn", [XNOR] = "xnor", [ROLr] = "rol", [RORr] = "ror",
  // RV32B Zbb: Bit counting (I-type unary: read rs1 only, write rd)
  [CLZr] = "clz", [CTZ] = "ctz", [CPOP] = "cpop",
  [SEXT_B] = "sext.b", [SEXT_H] = "sext.h", [ZEXT_H] = "zext.h",
  // Upper immediate
  [LUI] = "lui", [AUIPC] = "auipc",
  // Memory operations
  [LBr] = "lb", [LHr] = "lh", [LWr] = "lw", [LBUr] = "lbu", [LHUr] = "lhu",
  [SBr] = "sb", [SHr] = "sh", [SWr] = "sw",
  // Branches
  [BEQ] = "beq", [BNE] = "bne", [BLT] = "blt", [BGE] = "bge", 
  [BLTU] = "bltu", [BGEU] = "bgeu",
  // Jumps
  [JAL] = "jal", [JALR] = "jalr",
  // System
  [ECALL] = "ecall", [EBREAK] = "ebreak",
  // CSR instructions
  [CSRRW] = "csrrw", [CSRRS] = "csrrs", [CSRRC] = "csrrc",
  [CSRRWI] = "csrrwi", [CSRRSI] = "csrrsi", [CSRRCI] = "csrrci",
  // Custom DPU instructions
  [MYID] = "myid", [LDMRAM] = "ldmram", [SDMRAM] = "sdmram",
  // Fence
  [FENCE] = "fence",
};

// Low 2 bits: denotes read register count, 0~3
// 3rd bit: whether the opcode writes to rd
// EX: RvNeedRw[LUI] = 4 -- writes to rd but reads no registers
const uint8_t RvNeedRw[RvNrOpcode] = {
  // Arithmetic & Logic (R-type: read rs1+rs2, write rd)
  [ADDr] = 6, [SUBr] = 6, [ANDr] = 6, [ORr] = 6, [XORr] = 6,
  [SLL] = 6, [SRL] = 6, [SRA] = 6, [SLT] = 6, [SLTU] = 6,
  // RV32M: Multiplication & Division (R-type: read rs1+rs2, write rd)
  [MUL] = 6, [MULH] = 6, [MULHSU] = 6, [MULHU] = 6,
  [DIV] = 6, [DIVU] = 6, [REM] = 6, [REMU] = 6,
  // RV32B Zbb: Basic bit manipulation (R-type: read rs1+rs2, write rd)
  [MIN] = 6, [MAXr] = 6, [MINU] = 6, [MAXU] = 6,
  [ANDNr] = 6, [ORNr] = 6, [XNOR] = 6, [ROLr] = 6, [RORr] = 6,
  // RV32B Zbb: Bit counting (I-type unary: read rs1 only, write rd)
  [CLZr] = 5, [CTZ] = 5, [CPOP] = 5,
  [SEXT_B] = 5, [SEXT_H] = 5, [ZEXT_H] = 5,
  // Immediate variants (I-type: read rs1, write rd)
  [ADDI] = 5, [ANDI] = 5, [ORI] = 5, [XORI] = 5,
  [SLLI] = 5, [SRLI] = 5, [SRAI] = 5, [RORI] = 5,
  [SLTI] = 5, [SLTIU] = 5,
  // Upper immediate (U-type: no reads, write rd)
  [LUI] = 4, [AUIPC] = 4,
  // Memory loads (I-type: read rs1, write rd)
  [LBr] = 5, [LHr] = 5, [LWr] = 5, [LBUr] = 5, [LHUr] = 5,
  // Memory stores (S-type: read rs1+rs2, no rd write)
  [SBr] = 2, [SHr] = 2, [SWr] = 2,
  // Branches (B-type: read rs1+rs2, no rd write)
  [BEQ] = 2, [BNE] = 2, [BLT] = 2, [BGE] = 2, [BLTU] = 2, [BGEU] = 2,
  // Jumps
  [JAL] = 4,   // J-type: no reads, write rd
  [JALR] = 5,  // I-type: read rs1, write rd
  // System (no register access)
  [ECALL] = 0, [EBREAK] = 0,
  // CSR instructions (read rs1, write rd)
  [CSRRW] = 5, [CSRRS] = 5, [CSRRC] = 5,
  [CSRRWI] = 4, [CSRRSI] = 4, [CSRRCI] = 4,  // Immediate variants: no rs1
  // Custom DPU instructions
  [MYID] = 4,    // No reads, write rd
  [LDMRAM] = 7,  // Read rs1(wramAddr)+rd(mramAddr)+rs2(size), no rd write
  [SDMRAM] = 7,  // Read rs1(wramAddr)+rd(mramAddr)+rs2(size), no rd write
  // Fence (no register access)
  [FENCE] = 0,
};
