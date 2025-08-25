#include "downmem.h"

// Minimal RISC-V opcode strings for host API compatibility
const char* DmmOpStr[NrOpcode] = {
  // Arithmetic & Logic
  [ADD] = "add", [SUB] = "sub", [AND] = "and", [OR] = "or", [XOR] = "xor",
  [SLL] = "sll", [SRL] = "srl", [SRA] = "sra", [SLT] = "slt", [SLTU] = "sltu",
  // RV32M: Multiplication & Division
  [MUL] = "mul", [MULH] = "mulh", [MULHSU] = "mulhsu", [MULHU] = "mulhu",
  [DIV] = "div", [DIVU] = "divu", [REM] = "rem", [REMU] = "remu",
  // Immediate variants
  [ADDI] = "addi", [ANDI] = "andi", [ORI] = "ori", [XORI] = "xori",
  [SLLI] = "slli", [SRLI] = "srli", [SRAI] = "srai",
  [SLTI] = "slti", [SLTIU] = "sltiu",
  // RV32B Zbb: Basic bit manipulation (R-type: read rs1+rs2, write rd)
  [MIN] = "min", [MAX] = "max", [MINU] = "minu", [MAXU] = "maxu",
  [ANDN] = "andn", [ORN] = "orn", [XNOR] = "xnor", [ROL] = "rol", [ROR] = "ror",
  // RV32B Zbb: Bit counting (I-type unary: read rs1 only, write rd)
  [CLZ] = "clz", [CTZ] = "ctz", [CPOP] = "cpop",
  [SEXT_B] = "sext.b", [SEXT_H] = "sext.h", [ZEXT_H] = "zext.h",
  // Upper immediate
  [LUI] = "lui", [AUIPC] = "auipc",
  // Memory operations
  [LB] = "lb", [LH] = "lh", [LW] = "lw", [LBU] = "lbu", [LHU] = "lhu",
  [SB] = "sb", [SH] = "sh", [SW] = "sw",
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
// EX: DmmRvNeedRw[LUI] = 4 -- writes to rd but reads no registers
const uint8_t DmmRvNeedRw[NrOpcode] = {
  // Arithmetic & Logic (R-type: read rs1+rs2, write rd)
  [ADD] = 6, [SUB] = 6, [AND] = 6, [OR] = 6, [XOR] = 6,
  [SLL] = 6, [SRL] = 6, [SRA] = 6, [SLT] = 6, [SLTU] = 6,
  // RV32M: Multiplication & Division (R-type: read rs1+rs2, write rd)
  [MUL] = 6, [MULH] = 6, [MULHSU] = 6, [MULHU] = 6,
  [DIV] = 6, [DIVU] = 6, [REM] = 6, [REMU] = 6,
  // RV32B Zbb: Basic bit manipulation (R-type: read rs1+rs2, write rd)
  [MIN] = 6, [MAX] = 6, [MINU] = 6, [MAXU] = 6,
  [ANDN] = 6, [ORN] = 6, [XNOR] = 6, [ROL] = 6, [ROR] = 6,
  // RV32B Zbb: Bit counting (I-type unary: read rs1 only, write rd)
  [CLZ] = 5, [CTZ] = 5, [CPOP] = 5,
  [SEXT_B] = 5, [SEXT_H] = 5, [ZEXT_H] = 5,
  // Immediate variants (I-type: read rs1, write rd)
  [ADDI] = 5, [ANDI] = 5, [ORI] = 5, [XORI] = 5,
  [SLLI] = 5, [SRLI] = 5, [SRAI] = 5, [RORI] = 5,
  [SLTI] = 5, [SLTIU] = 5,
  // Upper immediate (U-type: no reads, write rd)
  [LUI] = 4, [AUIPC] = 4,
  // Memory loads (I-type: read rs1, write rd)
  [LB] = 5, [LH] = 5, [LW] = 5, [LBU] = 5, [LHU] = 5,
  // Memory stores (S-type: read rs1+rs2, no rd write)
  [SB] = 2, [SH] = 2, [SW] = 2,
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
