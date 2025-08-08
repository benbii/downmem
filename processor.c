#include "downmem.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __clang__
#define rotl32c __builtin_rotateleft32
#define rotr32c __builtin_rotateright32
#else
static uint32_t rotl32c (uint32_t x, uint32_t n) {
  assert (n<32);
  return (x<<n) | (x>>(-n&31));
}
static uint32_t rotr32c (uint32_t x, uint32_t n) {
  assert (n<32);
  return (x>>n) | (x<<(-n&31));
}
#endif

void DmmTletInit(DmmTlet* thread, uint8_t i) {
  thread->Pc = 0;
  thread->State = SLEEP;
  thread->Id = i;
  thread->Regs[NumGpRegisters + 0] = 0;
  thread->Regs[NumGpRegisters + 1] = 1;
  thread->Regs[NumGpRegisters + 2] = 0xffffffff;
  thread->Regs[NumGpRegisters + 3] = 0x80000000;
  thread->Regs[NumGpRegisters + 4] = (uint32_t)i;
  thread->Regs[NumGpRegisters + 5] = (uint32_t)i << 1;
  thread->Regs[NumGpRegisters + 6] = (uint32_t)i << 2;
  thread->Regs[NumGpRegisters + 7] = (uint32_t)i << 3;
}

void DmmDpuInit(DmmDpu* d, size_t memFreq, size_t logicFreq) {
  DmmPrgInit(&d->Program);
  DmmTimingInit(&d->Timing, d->Program.Iram, memFreq, logicFreq);
}

void DmmDpuRun(DmmDpu* d, size_t nrTasklets) {
  for (size_t i = 0; i < nrTasklets; ++i)
    d->Timing.Threads[i].Pc = 0;
  d->Timing.Threads[0].State = RUNNABLE;
  bool running = true;
  while (running) {
    // running = false;
    // for (size_t i = 0; i < nrTasklets; ++i) {
    //   if (d->Timing_.Threads[i].State != RUNNABLE)
    //     continue;
    //   running = true;
    //   DmmDpuExecuteInstr(d, &d->Timing_.Threads[i]);
    //   ++d->Timing_.StatNrInstrExec;
    // }
    DmmTlet *thrd = DmmTimingCycle(&d->Timing, nrTasklets);
    if (thrd != NULL)
      DmmDpuExecuteInstr(d, thrd);
    running = false;
    for (size_t i = 0; i < nrTasklets; ++i) {
      if (d->Timing.Threads[i].State == SLEEP)
        continue;
      running = true;
      break;
    }
  }
}

void DmmDpuExecuteInstr(DmmDpu* d, DmmTlet* thread) {
  assert(thread->Regs[ZeroReg] == 0);
  DmmInstr *instr = &d->Program.Iram[thread->Pc / IramDataByte];
  thread->Pc += IramDataByte;
  size_t rc = instr->RegC;
  uint8_t *wma = d->Program.WMAram;
  uint64_t va = thread->Regs[instr->RegA], vb = thread->Regs[instr->RegB],
           immA = instr->ImmA, result = 0;

  switch (instr->Opcode) {
  case LDMA: {
    __auto_type w = va & 0xfffff8;
    __auto_type m = vb & 0xfffffff8;
    // size_t N = (1 + (immA + (va >> 24) & 0xff) & 0xff) << 3;
    size_t N = (1 + immA + (va >> 24) & 0xff) << 3;
    memcpy(wma + w, wma + WramSize + m, N);
    return;
  }
  case LDMAI: exit(fputs("LDMAI not supported", stderr));
  case SDMA: {
    __auto_type w = va & 0xfffff8;
    __auto_type m = vb & 0xfffffff8;
    // size_t N = (1 + (immA + (vb >> 24) & 0xff) & 0xff) << 3;
    size_t N = (1 + immA + (va >> 24) & 0xff) << 3;
    memcpy(wma + WramSize + m, wma + w, N);
    return;
  }

  case MUL_STEP:
    // va is Rnx[0:31] in UMPEM DPU handbook, vb is Dp[32:63], rc is Dm
    assert(instr->Cond == Z && rc == 0 && instr->RegB == 0 &&
           "mul_step not in __mulsi3 function is unsupported");
    if (vb & 1)
      // thread->Regs[rc + 1] = thread->Regs[instr->RegB + 1] + (va << immA);
      thread->Regs[1] += va << immA;
    // if (0 == (thread->Regs[rc] = vb >> 1))
    if (0 == (thread->Regs[0] = vb >> 1))
      thread->Pc = instr->ImmB & IramMask;
    return;
  case DIV_STEP:
    // Dividend: rb[0:31] (r1); divisor: ra (r3);
    // quotient: rc[0:31] (r1); remainder: rc[32:63] (r0)
    // HACK: lol what a great division simulation :D
    if (immA == 0) {
      vb = thread->Regs[1]; // Dp
      thread->Regs[1] = vb % va;
      thread->Regs[0] = vb / va;
    }
    return;

  case MOVD:
    thread->Regs[rc] = (uint32_t)va;
    thread->Regs[rc+1] = thread->Regs[instr->RegA+1]; break;
  case SWAPD:
    thread->Regs[rc] = thread->Regs[instr->RegA+1];
    thread->Regs[rc+1] = (uint32_t)va; break;
  case MOVE: case MOVE_S: case MOVE_U:
    result = va + immA; break;

  case LD:
    result = *(uint64_t*)(wma + (uint32_t)(va + immA));
    thread->Regs[rc + 1] = (uint32_t)result;
    thread->Regs[rc] = (uint32_t)(result >> 32); return;
  case LW: case LW_S: case LW_U:
    result = (*(uint32_t*)(wma + (uint32_t)(va + immA))); break;
  case LHS: case LHS_S:
    result = (int64_t)(*(int16_t*)(wma + (uint32_t)(va + immA))); break;
  case LHU: case LHU_U:
    result = (*(uint16_t*)(wma + (uint32_t)(va + immA))); break;
  case LBS: case LBS_S:
    result = (int64_t)(int8_t)wma[(uint32_t)(va + immA)]; break;
  case LBU: case LBU_U:
    result = wma[(uint32_t)(va + immA)]; break;

  // FIXME: store db:64 ino ra+off
  case SD:
    assert(instr->RegB <= ZeroReg);
    if (instr->RegB == ZeroReg) {
      vb = (uint64_t)(int64_t)(int32_t)instr->ImmB;
    } else {
      vb = (uint64_t)thread->Regs[instr->RegB] << 32;
      vb += (uint64_t)thread->Regs[instr->RegB + 1];
    }
    *(uint64_t*)(wma + (uint32_t)(va + immA)) = vb; return;
  case SW: *(uint32_t*)(wma + (uint32_t)(va + immA)) = vb + instr->ImmB; return;
  case SH: *(uint16_t*)(wma + (uint32_t)(va + immA)) = vb + instr->ImmB; return;
  case SB: wma[(uint32_t)(va + immA)] = vb + instr->ImmB; return;
  case SD_ID:
    *(int64_t *)(wma + (uint32_t)(va + immA)) =
        thread->Id | (int64_t)instr->ImmB;
    return;
  case SW_ID:
    *(uint32_t *)(wma + (uint32_t)(va + immA)) = thread->Id | instr->ImmB;
    return;
  case SH_ID:
    *(uint16_t *)(wma + (uint32_t)(va + immA)) = thread->Id | instr->ImmB;
    return;
  case SB_ID: wma[(uint32_t)(va + immA)] = thread->Id | instr->ImmB; return;
  case EXTSB: case EXTSB_S: result = (uint64_t)(int64_t)(int8_t)va; break;
  case EXTSH: case EXTSH_S: result = (uint64_t)(int64_t)(int16_t)va; break;
  case EXTUB: case EXTUB_U: result = (uint64_t)(uint8_t)va; break;
  case EXTUH: case EXTUH_U: result = (uint64_t)(uint16_t)va; break;

  case MUL_SH_SH: case MUL_SH_SH_S: va >>= 8; // fallthrough
  case MUL_SL_SH: case MUL_SL_SH_S: vb >>= 8; // fallthrough
  case MUL_SL_SL: case MUL_SL_SL_S:
    result = (int64_t)(int8_t)va * (int64_t)(int8_t)vb; break;
  case MUL_SH_SL: case MUL_SH_SL_S:
    result = (int64_t)(int8_t)(va >> 8) * (int64_t)(int8_t)vb; break;
  case MUL_SH_UH: case MUL_SH_UH_S: va >>= 8; // fallthrough
  case MUL_SL_UH: case MUL_SL_UH_S: vb >>= 8; // fallthrough
  case MUL_SL_UL: case MUL_SL_UL_S:
    result = (int64_t)(int8_t)va * (vb & 255); break;
  case MUL_SH_UL: case MUL_SH_UL_S:
    result = (int64_t)(int8_t)(va >> 8) * (vb & 255); break;
  case MUL_UH_SH: case MUL_UH_SH_S: va >>= 8; // fallthrough
  case MUL_UL_SH: case MUL_UL_SH_S: vb >>= 8; // fallthrough
  case MUL_UL_SL: case MUL_UL_SL_S:
    result = (va & 255) * (int64_t)(int8_t)vb; break;
  case MUL_UH_SL: case MUL_UH_SL_S:
    result = ((va >> 8) & 255) * (int64_t)(int8_t)vb; break;
  case MUL_UH_UH: case MUL_UH_UH_U: va >>= 8; // fallthrough
  case MUL_UL_UH: case MUL_UL_UH_U: vb >>= 8; // fallthrough
  case MUL_UL_UL: case MUL_UL_UL_U: result = (va & 255) * (vb & 255); break;
  case MUL_UH_UL: case MUL_UH_UL_U: result = ((va >> 8) & 255) * (vb & 255); break;

  case CLS: case CLS_U: if (va & 0x80000000) { va = ~va; } // fallthrough
  case CLZ: case CLZ_U: result = va == 0 ? 32 : __builtin_clz((uint32_t)va); break;
  case CLO: case CLO_U: result = ~va == 0 ? 32 : __builtin_clz((uint32_t)~va); break;
  case CAO: case CAO_U: result = __builtin_popcount((uint32_t)va); break;

  case JMP: vb += immA; break;
  case CALL:
    thread->Regs[rc] = thread->Pc / IramDataByte;
    thread->Pc = (va * IramDataByte + immA) & IramMask; return;

  case ACQUIRE: case RELEASE:
    va = va + immA;
    va = (va ^ (va >> 8)) & 255;
    result = wma[WramSize + MramSize + va];
    wma[WramSize + MramSize + va] = instr->Opcode == ACQUIRE;
    break;
  case STOP: thread->State = SLEEP; break;
  case BOOT: case RESUME:
    va = va + immA;
    va = (va ^ (va >> 8)) & 31;
    result = d->Timing.Threads[va].State != SLEEP;
    if (result) break;
    d->Timing.Threads[va].State = RUNNABLE;
    if (instr->Opcode == BOOT)
      d->Timing.Threads[va].Pc = 0;
    break;

  case CLR_RUN: case TIME: case TIME_CFG: case FAULT:
  case HASH: case HASH_S: case HASH_U:
  case SATS: case SATS_S: case SATS_U:
  case CMPB4: case CMPB4_S: case CMPB4_U:
    exit(fprintf(stderr, "TODO: clrRun, timeCfg, dpuFault not implemented"));
  case NOP: return;

  case ADD: case ADD_S: case ADD_U:
    vb += immA; result = va + (uint32_t)vb;
    thread->CarryFlag = result >> 32 != 0; break;
  case ADDC: case ADDC_S: case ADDC_U:
    vb += immA; result = va + (uint32_t)vb + thread->CarryFlag;
    thread->CarryFlag = result >> 32 != 0; break;
  case SUB: case SUB_S: case SUB_U:
    vb += immA; result = va - vb;
    thread->CarryFlag = result >> 32 != 0; break;
  case SUBC: case SUBC_S: case SUBC_U:
    vb += immA + thread->CarryFlag; result = va - vb;
    thread->CarryFlag = result >> 32 != 0; break;
  case AND: case AND_S: case AND_U: result = va & (vb += immA); break;
  case NAND: case NAND_S: case NAND_U: result = ~(va & (vb += immA)); break;
  case ANDN: case ANDN_S: case ANDN_U: result = ~va & (vb += immA); break;
  case OR: case OR_S: case OR_U: result = va | (vb += immA); break;
  case NOR: case NOR_S: case NOR_U: result = ~(va | (vb += immA)); break;
  case ORN: case ORN_S: case ORN_U: result = ~va | (vb += immA); break;
  case XOR: case XOR_S: case XOR_U: result = va ^ (vb += immA); break;
  case NXOR: case NXOR_S: case NXOR_U: result = ~(va ^ (vb += immA)); break;
  case NEG: result = -va; break;
  case NOT: result = ~va; break;

  case ROL: case ROL_S: case ROL_U:
    result = rotl32c((uint32_t)va, (vb + immA) & 31); break;
  case ROR: case ROR_S: case ROR_U:
    result = rotr32c((uint32_t)va, (vb + immA) & 31); break;
  case LSL: case LSL_S: case LSL_U: result = va << ((vb += immA) & 31); break;
  case LSR: case LSR_S: case LSR_U: result = va >> ((vb += immA) & 31); break;
  case ASR: case ASR_S: case ASR_U:
    result = (int32_t)va >> ((vb + immA) & 31); break;
  // case LSL1: case LSL1_S: case LSL1_U:
  //   vb = (vb + immA) & 31;
  //   result = (va<<vb) | ((1<<vb)-1); break;
  // case LSR1: case LSR1_S: case LSR1_U:
  //   vb = (vb + immA) & 31;
  //   result = (va >> vb) | (~0ull << (32 - vb)); break;
  case LSLX: case LSLX_S: case LSLX_U:
    vb += immA; result = va << (vb & 31) >> 32; break;
  case LSRX: case LSRX_S: case LSRX_U:
    vb += immA; result = va << 32 >> (vb & 31); break;
  // case LSL1X: case LSL1X_S: case LSL1X_U:
  //   vb = (vb + immA) & 31;
  //   result = (0xffffffffull << vb) | (va << vb >> 32); break;
  // case LSR1X: case LSR1X_S: case LSR1X_U:
  //   vb = (vb + immA) & 31;
  //   result = (0xffffffffull >> vb) | (va << 32 >> vb); break;

  case ROL_ADD: case ROL_ADD_S: case ROL_ADD_U:
    result = rotl32c((uint32_t)vb, immA & 31); break;
  case LSR_ADD: case LSR_ADD_S: case LSR_ADD_U:
    result = vb >> (immA & 31); break;
  case LSL_SUB: case LSL_SUB_S: case LSL_SUB_U:
    result = -(vb << (immA & 31)); break;
  case LSL_ADD: case LSL_ADD_S: case LSL_ADD_U:
    result = vb << (immA & 31); break;
  default:
    assert(false && "Opcode not suported");
    __builtin_unreachable();
  }

  // Write back
  switch (DmmOpWbMode[instr->Opcode]) {
    case noWb: break;
    case wbZf:
    case wbNoZf: thread->Regs[rc] = (uint32_t)result; break;
    case wbZf_s:
    case wbNoZf_s:
      result = (int64_t)(int32_t)result;
      thread->Regs[rc] = result >> 32;
      thread->Regs[rc + 1] = result; break;
    case wbZf_u:
    case wbNoZf_u:
      thread->Regs[rc] = 0;
      thread->Regs[rc + 1] = result; break;

    case wbShAdd:
      result += va;
      thread->Regs[rc] = (uint32_t)result; break;
    case wbShAdd_u:
      result += va;
      thread->Regs[rc + 1] = (uint32_t)result;
      thread->Regs[rc] = 0; break;
    case wbShAdd_s:
      result = (int64_t)(int32_t)(result + va);
      thread->Regs[rc + 1] = (uint32_t)result;
      thread->Regs[rc] = (uint32_t)(result >> 32); break;
    default: __builtin_unreachable();
  }

  // Handle conditions
  bool condMet;
  switch (instr->Cond) {
  case NoCond: return;
  case TRUE: condMet = true; break;
  case FALSE: condMet = false; break;
  case Z: condMet = (uint32_t)result == 0; break;
  case NZ: condMet = (uint32_t)result != 0; break;
  case SZ: condMet = va == 0; break;
  case SNZ: condMet = va != 0; break;
  case PL: condMet = (int32_t)result >= 0; break;
  case MI: condMet = (int32_t)result < 0; break;
  case SPL: condMet = (int32_t)va >= 0; break;
  case SMI: condMet = (int32_t)va < 0; break;
  // Overflow not supported
  // Carry condition (not carry flag) on subtraction not supported
  case C: condMet = result >> 32 != 0; break;
  case NC: condMet = result >> 32 == 0; break;
  case LTU: condMet = (uint32_t)va <  (uint32_t)vb; break;
  case GEU: condMet = (uint32_t)va >= (uint32_t)vb; break;
  case LEU: condMet = (uint32_t)va <= (uint32_t)vb; break;
  case GTU: condMet = (uint32_t)va >  (uint32_t)vb; break;
  case LTS: condMet = (int32_t)va <  (int32_t)vb; break;
  case GES: condMet = (int32_t)va >= (int32_t)vb; break;
  case LES: condMet = (int32_t)va <= (int32_t)vb; break;
  case GTS: condMet = (int32_t)va >  (int32_t)vb; break;
  case EQ: condMet = va == vb; break;
  case NEQ: condMet = va != vb; break;

  // HACK: they are only useful for 64b comparisons
  case XZ: case NXZ: case XLEU: case XGTU: case XLES: case XGTS:
    va = (va << 32) + thread->Regs[instr->RegA + 1];
    vb = ((uint64_t)thread->Regs[instr->RegB] << 32) +
         thread->Regs[instr->RegB + 1];
    switch (instr->Cond) {
    case XZ: condMet = va == vb; break;
    case NXZ: condMet = va != vb; break;
    case XLEU: condMet = va <= vb; break;
    case XGTU: condMet = va > vb; break;
    case XLES: condMet = (int32_t)va <= (int32_t)vb; break;
    case XGTS: condMet = (int32_t)va > (int32_t)vb; break;
    default: __builtin_unreachable();
    }
    break;

  // less common ones
  case SE: condMet = !(va & 1); break;
  case SO: condMet =  (va & 1); break;
  case NSH32: condMet = !(vb & 32); break;
  case  SH32: condMet =  (vb & 32); break;
  case  MAX: condMet = result == (instr->Opcode == CLS ? 31 : 32); break;
  case NMAX: condMet = result != (instr->Opcode == CLS ? 31 : 32); break;
  case SMALL: condMet = !((va | vb) & 0xff00); break;
  case LARGE: condMet =  ((va | vb) & 0xff00); break;
  default:
    assert(false && "%d condition not supported\n");
    // exit(fprintf(stderr, "%d condition not supported\n", instr->Cond));
    __builtin_unreachable();
  }

  if (instr->ImmB > IramMask) {
    if (condMet)
      thread->Pc = instr->ImmB & IramMask;
  } else {
    thread->Regs[rc] = (uint32_t)condMet;
  }
}

