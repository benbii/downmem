#include "dmminternal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void RvTletInit(RvTlet* thread, uint8_t i) {
  thread->Pc = IramBeginR;
  thread->Id = i;
  // Initialize all registers to 0 (x0 is hardwired to 0)
  for (int reg = 0; reg < NumGpRegistersR; reg++)
    thread->Regs[reg] = 0;
}

void RvDpuInit(RvDpu* d, size_t memFreq, size_t logicFreq, int numaNode) {
  RvPrgInit(&d->Program, numaNode);
  RvTimingInit(&d->Timing, d->Program.Iram, memFreq, logicFreq);
  // CSR is now initialized in RvTimingInit
}

void RvDpuRun(RvDpu* d, size_t nrTasklets) {
  for (size_t i = 0; i < nrTasklets; ++i)
    d->Timing.Threads[i].Pc = IramBeginR;
  // Clear blocked bits and set running bits for all threads
  d->Timing.Csr[0] = (1 << nrTasklets) - 1;
  d->Timing.Csr[NrCsr - 1] = 0;

  uint32_t running = true;
  while (running) {
#ifdef __DMM_FUNCTIONAL_ONLY
    running = false;
    for (size_t i = 0; i < nrTasklets; ++i) {
      // Check if thread is sleeping or blocked via CSR bits
      if (!((d->Timing.Csr[0] >> i) & 1)) continue;     // sleeping
      if ((d->Timing.Csr[31] >> i) & 1) continue;    // blocked
      running = true;
      RvDpuExecuteInstr(d, &d->Timing.Threads[i]);
      ++d->Timing.StatNrInstrExec;
    }
#else
    RvTlet *thrd = RvTimingCycle(&d->Timing, nrTasklets);
    if (thrd != NULL)
      RvDpuExecuteInstr(d, thrd);
    running = d->Timing.Csr[0] & ((1 << nrTasklets) - 1);
#endif
  }
}

void RvDpuExecuteInstr(RvDpu* d, RvTlet* thread) {
  RvInstr *instr = &d->Program.Iram[(thread->Pc - IramBeginR) / InstrNrByteR];
  thread->Pc += InstrNrByteR;  // Increment PC early
  size_t rd = instr->rd;
  uint8_t *wm = d->Program.WMAram;
  uint32_t vs1 = thread->Regs[instr->rs1], vs2 = thread->Regs[instr->rs2], result;
  int32_t imm = instr->imm;

  switch (instr->Opcode) {
  // Arithmetic & Logic
  case ADDr: result = vs1 + vs2; break;
  case SUBr: result = vs1 - vs2; break;
  case ANDr: result = vs1 & vs2; break;
  case ORr:  result = vs1 | vs2; break;
  case XORr: result = vs1 ^ vs2; break;
  case SLL: result = vs1 << (vs2 & 0x1F); break;
  case SRL: result = vs1 >> (vs2 & 0x1F); break;
  case SRA: result = (int32_t)vs1 >> (vs2 & 0x1F); break;
  case SLT: result = (int32_t)vs1 < (int32_t)vs2 ? 1 : 0; break;
  case SLTU: result = vs1 < vs2 ? 1 : 0; break;

  // RV32M: Multiplication & Division placeholder;
  // the hardware cannot finish mul & div in 1 single cycle :D
  case MUL: result = vs1 * vs2; break;
  case MULH:
    result = ((int64_t)(int32_t)vs1 * (int64_t)(int32_t)vs2 >> 32); break;
  case MULHSU:
    result = ((int64_t)(int32_t)vs1 * (uint64_t)vs2) >> 32; break;
  case MULHU:
    result = (uint64_t)vs1 * (uint64_t)vs2 >> 32; break;
  case DIV:
    if (vs2 == 0) result = -1;
    else if (vs1 == 0x80000000 && vs2 == 0xFFFFFFFF) result = 0x80000000;
    else result = (int32_t)vs1 / (int32_t)vs2;
    break;
  case DIVU:
    result = vs2 == 0 ? 0xFFFFFFFF : vs1 / vs2; break;
  case REM:
    if (vs2 == 0) result = vs1;
    else if (vs1 == 0x80000000 && vs2 == 0xFFFFFFFF) result = 0;
    else result = (int32_t)vs1 % (int32_t)vs2;
    break;
  case REMU:
    result = vs2 == 0 ? vs1 : vs1 % vs2;
    break;

  // RV32B Zbb: Basic bit manipulation (min/max)
  case MIN: result = ((int32_t)vs1 < (int32_t)vs2) ? vs1 : vs2; break;
  case MAXr: result = ((int32_t)vs1 > (int32_t)vs2) ? vs1 : vs2; break;
  case MINU: result = (vs1 < vs2) ? vs1 : vs2; break;
  case MAXU: result = (vs1 > vs2) ? vs1 : vs2; break;

  // RV32B Zbb: Bit counting and manipulation
  case CLZr: result = vs1 ? __builtin_clz(vs1) : 32; break;  // Count leading zeros
  case CTZ: result = vs1 ? __builtin_ctz(vs1) : 32; break;  // Count trailing zeros
  case CPOP: result = __builtin_popcount(vs1); break;       // Population count
  // RV32B Zbb: Sign/zero extension
  case SEXT_B: result = (int32_t)(int8_t)vs1; break;        // Sign-extend byte
  case SEXT_H: result = (int32_t)(int16_t)vs1; break;       // Sign-extend halfword
  case ZEXT_H: result = vs1 & 0xFFFF; break;                // Zero-extend halfword
  // RV32B Zbb: Bitwise NOT operations
  case ANDNr: result = vs1 & ~vs2; break;                    // Bitwise AND-NOT
  case ORNr: result = vs1 | ~vs2; break;                     // Bitwise OR-NOT
  case XNOR: result = ~(vs1 ^ vs2); break;                  // Bitwise XOR-NOT
  // RV32B Zbb: Rotate operations
  case ROLr: result = __builtin_rotateleft32(vs1, vs2 & 31); break;  // Rotate left
  case RORr: result = __builtin_rotateright32(vs1, vs2 & 31); break;  // Rotate right
  // case ROLr: result = (vs1 << (vs2 & 0x1F)) | (vs1 >> (32 - (vs2 & 0x1F))); break;  // Rotate left
  // case RORr: result = (vs1 >> (vs2 & 0x1F)) | (vs1 << (32 - (vs2 & 0x1F))); break;  // Rotate right

  // Immediate variants
  case ADDI: result = vs1 + imm; break;
  case ANDI: result = vs1 & imm; break;
  case ORI:  result = vs1 | imm; break;
  case XORI: result = vs1 ^ imm; break;
  case SLLI: result = vs1 << (imm & 0x1F); break;
  case SRLI: result = vs1 >> (imm & 0x1F); break;
  case SRAI: result = (int32_t)vs1 >> (imm & 0x1F); break;
  case RORI: result = (vs1 >> (imm & 0x1F)) | (vs1 << (32 - (imm & 0x1F))); break;  // Rotate right immediate
  case SLTI: result = (int32_t)vs1 < (int32_t)imm ? 1 : 0; break;
  case SLTIU: result = vs1 < imm ? 1 : 0; break;

  // Upper immediate
  case LUI: result = imm; break;
  case AUIPC: result = (thread->Pc - InstrNrByteR) + imm; break;

  // Memory operations
  case LBr:
    if (vs1 + imm >= WramSizeR) imm -= (MramBeginR - WramSizeR);
    result = (int32_t)(int8_t)wm[vs1 + imm]; break;
  case LHr:
    if (vs1 + imm >= WramSizeR) imm -= (MramBeginR - WramSizeR);
    result = (int32_t)(int16_t)*(uint16_t*)(wm + vs1 + imm); break;
  case LWr:
    if (vs1 + imm >= WramSizeR) imm -= (MramBeginR - WramSizeR);
    result = *(uint32_t*)(wm + vs1 + imm); break;
  case LBUr:
    if (vs1 + imm >= WramSizeR) imm -= (MramBeginR - WramSizeR);
    result = wm[vs1 + imm]; break;
  case LHUr:
    if (vs1 + imm >= WramSizeR) imm -= (MramBeginR - WramSizeR);
    result = *(uint16_t*)(wm + vs1 + imm); break;
  case SBr:
    if (vs1 + imm >= WramSizeR) imm -= (MramBeginR - WramSizeR);
    wm[vs1 + imm] = vs2; return;
  case SHr:
    if (vs1 + imm >= WramSizeR) imm -= (MramBeginR - WramSizeR);
    *(uint16_t *)(wm + vs1 + imm) = vs2; return;
  case SWr:
    if (vs1 + imm >= WramSizeR) imm -= (MramBeginR - WramSizeR);
    *(uint32_t *)(wm + vs1 + imm) = vs2; return;

  // Branches (modify PC, don't write rd)
  case BEQ:
    if (vs1 == vs2) thread->Pc = (thread->Pc - InstrNrByteR) + imm;
    rd = 0; break;
  case BNE:
    if (vs1 != vs2) thread->Pc = (thread->Pc - InstrNrByteR) + imm;
    rd = 0; break;
  case BLT:
    if ((int32_t)vs1 < (int32_t)vs2) thread->Pc = (thread->Pc - InstrNrByteR) + imm;
    rd = 0; break;
  case BGE:
    if ((int32_t)vs1 >= (int32_t)vs2) thread->Pc = (thread->Pc - InstrNrByteR) + imm;
    rd = 0; break;
  case BLTU:
    if (vs1 < vs2) thread->Pc = (thread->Pc - InstrNrByteR) + imm;
    rd = 0; break;
  case BGEU:
    if (vs1 >= vs2) thread->Pc = (thread->Pc - InstrNrByteR) + imm;
    rd = 0; break;

  // Jumps
  case JAL:
    result = thread->Pc; // save return address (current PC)
    thread->Pc = (thread->Pc - InstrNrByteR) + imm; break;
  case JALR:
    result = thread->Pc; // save return address (current PC)
    thread->Pc = (vs1 + imm) & ~1;  // clear LSB
    break;

  case ECALL: case EBREAK:
    assert(0 && "DPU Trapped!");
  // Fence (memory ordering) - for now treat as NOP
  case FENCE: rd = 0; break;

  // CSR instructions (use DPU CSR array, imm contains CSR address)
  case CSRRS:
    result = d->Timing.Csr[imm % NrCsr];
    d->Timing.Csr[imm % NrCsr] |= vs1; break;
  case CSRRC:
    result = d->Timing.Csr[imm % NrCsr];
    d->Timing.Csr[imm % NrCsr] &= ~vs1; break;
  case CSRRWI:
    result = d->Timing.Csr[imm % NrCsr];
    d->Timing.Csr[imm % NrCsr] = instr->rs1; break;
  case CSRRCI:
    result = d->Timing.Csr[imm % NrCsr];
    d->Timing.Csr[imm % NrCsr] &= ~instr->rs1; break;

  // special operations involving CSRRSI and CSRRW
  case CSRRSI:
    imm %= NrCsr;
    result = d->Timing.Csr[imm];
    if (imm == 20) result = thread->Id;
    if (imm == 0) result = d->Timing.StatNrCycle;
    if (imm == 2) result = d->Timing.StatNrInstrExec;
    d->Timing.Csr[imm] |= instr->rs1; break;
  case CSRRW:
    imm %= NrCsr;
    if (imm != 3) {
      result = d->Timing.Csr[imm];
      d->Timing.Csr[imm] = vs1;
      break; // from hardware standpoint no "break" is more suitable
    }
    uint8_t *wram_addr = wm + (vs1 >> 16);
    uint8_t *mram_addr =
        wm + WramSizeR + (thread->Regs[instr->rd] - MramBeginR & MramMaskR);
    memcpy((vs1 & 32768) ? mram_addr : wram_addr,
           (vs1 & 32768) ? wram_addr : mram_addr, vs1 & 32767);
    rd = 0; break;

  default: __builtin_unreachable();
  }
  // Write result back to rd (unless it's x0 or instruction doesn't write)
  if (rd != 0) thread->Regs[rd] = result;
}
