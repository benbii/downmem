#include "downmem.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void DmmTletInit(DmmTlet* thread, uint8_t i) {
  thread->Pc = IramBegin;
  thread->Id = i;
  // Initialize all registers to 0 (x0 is hardwired to 0)
  for (int reg = 0; reg < NumGpRegisters; reg++)
    thread->Regs[reg] = 0;
}

void DmmDpuInit(DmmDpu* d, size_t memFreq, size_t logicFreq) {
  DmmPrgInit(&d->Program);
  DmmTimingInit(&d->Timing, d->Program.Iram, memFreq, logicFreq);
  // CSR is now initialized in DmmTimingInit
}

void DmmDpuRun(DmmDpu* d, size_t nrTasklets) {
  for (size_t i = 0; i < nrTasklets; ++i)
    d->Timing.Threads[i].Pc = IramBegin;
  // Clear blocked bits and set running bits for all threads
  d->Timing.Csr[0] = (1 << nrTasklets) - 1;
  d->Timing.Csr[AtomicSize - 1] = 0;

  uint32_t running = true;
  while (running) {
#ifdef __DMM_FUNCTIONAL_ONLY
    running = false;
    for (size_t i = 0; i < nrTasklets; ++i) {
      // Check if thread is sleeping or blocked via CSR bits
      if (!((d->Timing.Csr[0] >> i) & 1)) continue;     // sleeping
      if ((d->Timing.Csr[31] >> i) & 1) continue;    // blocked
      running = true;
      DmmDpuExecuteInstr(d, &d->Timing.Threads[i]);
      ++d->Timing.StatNrInstrExec;
    }
#else
    DmmTlet *thrd = DmmTimingCycle(&d->Timing, nrTasklets);
    if (thrd != NULL)
      DmmDpuExecuteInstr(d, thrd);
    running = d->Timing.Csr[0] & ((1 << nrTasklets) - 1);
#endif
  }
}

void DmmDpuExecuteInstr(DmmDpu* d, DmmTlet* thread) {
  DmmInstr *instr = &d->Program.Iram[(thread->Pc - IramBegin) / InstrNrByte];
  thread->Pc += InstrNrByte;  // Increment PC early
  size_t rd = instr->rd;
  uint8_t *wm = d->Program.WMAram;
  uint32_t vs1 = thread->Regs[instr->rs1], vs2 = thread->Regs[instr->rs2], result;
  int32_t imm = instr->imm;

  switch (instr->Opcode) {
  // Arithmetic & Logic
  case ADD: result = vs1 + vs2; break;
  case SUB: result = vs1 - vs2; break;
  case AND: result = vs1 & vs2; break;
  case OR:  result = vs1 | vs2; break;
  case XOR: result = vs1 ^ vs2; break;
  case SLL: result = vs1 << (vs2 & 0x1F); break;
  case SRL: result = vs1 >> (vs2 & 0x1F); break;
  case SRA: result = (int32_t)vs1 >> (vs2 & 0x1F); break;
  case SLT: result = (int32_t)vs1 < (int32_t)vs2 ? 1 : 0; break;
  case SLTU: result = vs1 < vs2 ? 1 : 0; break;

  // RV32M: Multiplication & Division
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
  case MAX: result = ((int32_t)vs1 > (int32_t)vs2) ? vs1 : vs2; break;
  case MINU: result = (vs1 < vs2) ? vs1 : vs2; break;
  case MAXU: result = (vs1 > vs2) ? vs1 : vs2; break;
  
  // RV32B Zbb: Bit counting and manipulation
  case CLZ: result = vs1 ? __builtin_clz(vs1) : 32; break;  // Count leading zeros
  case CTZ: result = vs1 ? __builtin_ctz(vs1) : 32; break;  // Count trailing zeros
  case CPOP: result = __builtin_popcount(vs1); break;       // Population count
  
  // RV32B Zbb: Sign/zero extension
  case SEXT_B: result = (int32_t)(int8_t)vs1; break;        // Sign-extend byte
  case SEXT_H: result = (int32_t)(int16_t)vs1; break;       // Sign-extend halfword
  case ZEXT_H: result = vs1 & 0xFFFF; break;                // Zero-extend halfword
  
  // RV32B Zbb: Bitwise NOT operations
  case ANDN: result = vs1 & ~vs2; break;                    // Bitwise AND-NOT
  case ORN: result = vs1 | ~vs2; break;                     // Bitwise OR-NOT
  case XNOR: result = ~(vs1 ^ vs2); break;                  // Bitwise XOR-NOT
  
  // RV32B Zbb: Rotate operations
  case ROL: result = (vs1 << (vs2 & 0x1F)) | (vs1 >> (32 - (vs2 & 0x1F))); break;  // Rotate left
  case ROR: result = (vs1 >> (vs2 & 0x1F)) | (vs1 << (32 - (vs2 & 0x1F))); break;  // Rotate right

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
  case AUIPC: result = (thread->Pc - InstrNrByte) + imm; break;

  // Memory operations
  case LB:
    if (vs1 + imm >= WramSize) imm -= (MramBegin - WramSize);
    result = (int32_t)(int8_t)wm[vs1 + imm]; break;
  case LH:
    if (vs1 + imm >= WramSize) imm -= (MramBegin - WramSize);
    result = (int32_t)(int16_t)*(uint16_t*)(wm + vs1 + imm); break;
  case LW:
    if (vs1 + imm >= WramSize) imm -= (MramBegin - WramSize);
    result = *(uint32_t*)(wm + vs1 + imm); break;
  case LBU:
    if (vs1 + imm >= WramSize) imm -= (MramBegin - WramSize);
    result = wm[vs1 + imm]; break;
  case LHU:
    if (vs1 + imm >= WramSize) imm -= (MramBegin - WramSize);
    result = *(uint16_t*)(wm + vs1 + imm); break;
  case SB:
    if (vs1 + imm >= WramSize) imm -= (MramBegin - WramSize);
    wm[vs1 + imm] = vs2; return;
  case SH:
    if (vs1 + imm >= WramSize) imm -= (MramBegin - WramSize);
    *(uint16_t *)(wm + vs1 + imm) = vs2; return;
  case SW:
    if (vs1 + imm >= WramSize) imm -= (MramBegin - WramSize);
    *(uint32_t *)(wm + vs1 + imm) = vs2; return;

  // Branches (modify PC, don't write rd)
  case BEQ:
    if (vs1 == vs2) thread->Pc = (thread->Pc - InstrNrByte) + imm;
    rd = 0; break;
  case BNE:
    if (vs1 != vs2) thread->Pc = (thread->Pc - InstrNrByte) + imm;
    rd = 0; break;
  case BLT:
    if ((int32_t)vs1 < (int32_t)vs2) thread->Pc = (thread->Pc - InstrNrByte) + imm;
    rd = 0; break;
  case BGE:
    if ((int32_t)vs1 >= (int32_t)vs2) thread->Pc = (thread->Pc - InstrNrByte) + imm;
    rd = 0; break;
  case BLTU:
    if (vs1 < vs2) thread->Pc = (thread->Pc - InstrNrByte) + imm;
    rd = 0; break;
  case BGEU:
    if (vs1 >= vs2) thread->Pc = (thread->Pc - InstrNrByte) + imm;
    rd = 0; break;

  // Jumps
  case JAL:
    result = thread->Pc; // save return address (current PC)
    thread->Pc = (thread->Pc - InstrNrByte) + imm; break;
  case JALR:
    result = thread->Pc; // save return address (current PC)
    thread->Pc = (vs1 + imm) & ~1;  // clear LSB
    break;

  case ECALL: case EBREAK:
    assert(0 && "DPU Trapped!");
  // Fence (memory ordering) - for now treat as NOP
  case FENCE: rd = 0; break;

  // CSR instructions (use DPU CSR array, imm contains CSR address)
  case CSRRW:
    result = d->Timing.Csr[imm % AtomicSize];
    d->Timing.Csr[imm % AtomicSize] = vs1; break;
  case CSRRS:
    result = d->Timing.Csr[imm % AtomicSize];
    d->Timing.Csr[imm % AtomicSize] |= vs1; break;
  case CSRRC:
    result = d->Timing.Csr[imm % AtomicSize];
    d->Timing.Csr[imm % AtomicSize] &= ~vs1; break;
  case CSRRWI:
    result = d->Timing.Csr[imm % AtomicSize];
    d->Timing.Csr[imm % AtomicSize] = instr->rs1; break;
  case CSRRSI:
    result = d->Timing.Csr[imm % AtomicSize];
    d->Timing.Csr[imm % AtomicSize] |= instr->rs1; break;
  case CSRRCI:
    result = d->Timing.Csr[imm % AtomicSize];
    d->Timing.Csr[imm % AtomicSize] &= ~instr->rs1; break;

  // Custom DPU instructions
  case MYID: result = thread->Id; break;
  case LDMRAM: {
    uint32_t wram_addr = vs1 & WramMask;
    uint32_t mram_addr = (thread->Regs[instr->rd] - MramBegin) & MramMask;
    uint32_t size = vs2;
    assert(wram_addr + size <= WramSize && mram_addr + size <= MramSize);
    memcpy(wm + wram_addr, wm + WramSize + mram_addr, size);
    rd = 0;
    break;
  }
  case SDMRAM: {
    uint32_t wram_addr = vs1 & WramMask;
    uint32_t mram_addr = (thread->Regs[instr->rd] - MramBegin) & MramMask;
    uint32_t size = vs2;
    assert(wram_addr + size <= WramSize && mram_addr + size <= MramSize);
    memcpy(wm + WramSize + mram_addr, wm + wram_addr, size);
    rd = 0;
    break;
  }

  default:
    fprintf(stderr, "Unsupported opcode: %d\n", instr->Opcode);
    rd = 0; break;
  }

  // Write result back to rd (unless it's x0 or instruction doesn't write)
  if (rd != 0) thread->Regs[rd] = result;
}
