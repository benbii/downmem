#include "dmminternal.h"
#include <stdio.h>

static void servePipeline(RvTiming *this) {
  // If there's space in pipeline (i.e. not full) push the incoming instruction
  // The incoming instruction may not be set by scheduler and therefore
  // (RvInstr*)1
  if (((this->PpQRear - this->PpQFrt) & 15) < NrPipelineStage - 1) {
    this->PpInsideInstrs[this->PpQRear] = this->PpInInstr;
    this->PpInsideIds[this->PpQRear] = this->PpInId;
    this->PpQRear = (this->PpQRear + 1) & 15;
    this->PpInInstr = (RvInstr *)1;
  }
  // If no instruction is ready, pop from the queue
  if (this->PpReadyInstr == NULL) {
    RvInstr *instr = this->PpInsideInstrs[this->PpQFrt];
    long id = this->PpInsideIds[this->PpQFrt];
    this->PpQFrt = (this->PpQFrt + 1) & 15;
    this->PpReadyInstr = instr;
    this->PpReadyId = id;
  }
}

static void serveCycleRule(RvTiming *this) {
  if (this->CrCurInstr != NULL && this->CrPrevInstr == NULL) {
    RvInstr *instr = this->CrCurInstr;
    this->CrCurInstr = NULL;
    long thread_id = this->CrCurId;
    RvOpcode op = instr->Opcode;

    // RISC-V cycle rules: simpler than UPMEM since no 64-bit register pairs
    // All RISC-V instructions read at most 2 registers (rs1, rs2)
    uint64_t curRead = 0;
    // Add read registers (rs1, rs2) - all are single 32-bit registers
    curRead |= (1ull << instr->rs1);
    curRead |= (1ull << instr->rs2);
    // Custom DPU instructions may read rd
    if (op == LDMRAM || op == SDMRAM)
        curRead |= (1ull << instr->rd);
    // Include previous write register conflicts
    curRead |= this->CrPrevWriteRegSets[thread_id];
    // RISC-V register binning: r0 excluded
    int even_counter = __builtin_popcountll(curRead & 0x55555554);
    int odd_counter = __builtin_popcountll(curRead & 0xAAAAAAAA);

    // Extra cycles needed if >=2 registers accessed in same bin
    this->CrExtraCycleLeft =
        (even_counter >= 2 ? 1 : 0) + (odd_counter >= 2 ? 1 : 0);
    this->CrPrevInstr = instr;
    this->CrPrevId = thread_id;
    this->StatCycleRule += 1;
  }

  if (this->CrPrevInstr != NULL && this->CrExtraCycleLeft <= 0) {
    RvInstr *instr = this->CrPrevInstr;
    this->CrPrevInstr = NULL;
    long thread_id = this->CrPrevId;

    // Set write register for next cycle rule check
    // All RISC-V instructions write to single 32-bit register (rd)
    this->CrPrevWriteRegSets[thread_id] = (1ull << instr->rd);
  }
  this->CrExtraCycleLeft -= 1;
}

void RvTimingInit(RvTiming *t, RvInstr *iram, size_t memFreq,
                   size_t logicFreq) {
  memset(t, 0, sizeof(RvTiming));
  t->Iram = iram;
  for (long i = 0; i < MaxNumTasklets; i++) {
    RvTletInit(&t->Threads[i], i);
#ifdef __DMM_TSCDUMP
    t->lastPc[i] = IramNrInstrR - 1;
#endif
  }
  t->FreqRatio = (double)memFreq / (double)logicFreq;
  DmmMramTimingInit(&t->MramTiming);

  // Push dummy entries to simulate initial pipeline stages
  for (size_t i = 0; i < NrPipelineStage - 1; i++) {
    t->PpInsideInstrs[i] = (RvInstr *)1;
    t->PpInsideIds[i] = 0;
  }
  t->PpQFrt = 0;
  t->PpQRear = NrPipelineStage - 1;
  t->PpInInstr = (RvInstr *)1;
  t->PpReadyInstr = (RvInstr *)1;
}

RvTlet *RvTimingCycle(RvTiming *this, size_t nrTasklets) {
  long num_memory_cycles =
      (long)(this->FreqRatio * (double)this->TotNrCycle -
             this->FreqRatio * (double)(this->TotNrCycle - 1));
  for (long i = 0; i < num_memory_cycles; i++)
    DmmMramTimingCycle(&this->MramTiming);
  this->TotNrCycle++;
  this->StatNrCycle++;
  RvTlet *ret = NULL;

  if (this->PpInInstr != (RvInstr *)1 || this->CrCurInstr != NULL) {
    this->StatNrRfHazard += 1;
  } else {
    bool is_blocked = false;
    for (long i = 0; i < nrTasklets; i++) {
      RvTlet *thread = &this->Threads[this->lastIssue];
      this->lastIssue++;
      if (this->lastIssue == nrTasklets) this->lastIssue = 0;
      if (this->lastRunAt[this->lastIssue] + NrRevolveCycle >
          this->TotNrCycle)
        continue;

      // Check if thread is sleeping or blocked via CSR bits
      if (!((this->Csr[0] >> thread->Id) & 1)) // sleeping
        continue;
      if ((this->Csr[31] >> thread->Id) & 1) { // blocked
        is_blocked = true; continue;
      }

      size_t pc = (thread->Pc - IramBeginR) / InstrNrByteR;
      if (pc >= IramNrInstrR)
        continue; // PC out of bounds

      RvInstr *instr = &this->Iram[pc];
      this->PpInInstr = instr;
      this->PpInId = thread->Id;

      // Handle RISC-V custom DMA instructions (LDMRAM, SDMRAM)
      if (instr->Opcode == LDMRAM || instr->Opcode == SDMRAM) {
        // Extract DMA parameters from registers
        uint32_t wram_addr = thread->Regs[instr->rd] & WramMaskR;
        uint32_t mram_addr = (thread->Regs[instr->rs1] - MramBeginR) & MramMaskR;
        uint32_t size = thread->Regs[instr->rs2];
        if (size > 0) {
          // Push DMA request to MRAM timing simulator
          DmmMramTimingPush(&this->MramTiming, mram_addr, size, thread->Id);
          // Block thread until DMA completes
          this->Csr[31] |= (1 << thread->Id); // Set blocked bit
        }
      }

      static uint8_t loadOp[RvNrOpcode] = {
          [LBr] = 1,  [LBUr] = 1, [SBr] = 1, [LHr] = 2,
          [LHUr] = 2, [SHr] = 2,  [LWr] = 4, [SWr] = 4,
      };

      // Handle MRAM load/store operations (reg = *mram_ptr constructs)
      if (loadOp[instr->Opcode]) {
        // Calculate effective address: rs1 + immediate
        uint32_t addr = thread->Regs[instr->rs1] + instr->imm;
        // Check if address is in MRAM range
        if (addr >= MramBeginR && addr < MramBeginR + MramSizeR) {
          // MRAM access detected - push 8-byte request regardless of actual size
          uint32_t mram_addr = (addr - MramBeginR) & MramMaskR;
          DmmMramTimingPush(&this->MramTiming, mram_addr, loadOp[instr->Opcode],
                            thread->Id);
          // Block thread until MRAM access completes
          this->Csr[31] |= (1 << thread->Id); // Set blocked bit
        }
      }

      // Track instruction timing statistics
#ifdef __DMM_TSCDUMP
      this->StatTsc[this->lastPc[this->lastIssue]] +=
          this->TotNrCycle - this->lastRunAt[this->lastIssue];
      this->lastPc[this->lastIssue] = pc;
#endif
      this->lastRunAt[this->lastIssue] = this->TotNrCycle;
      this->StatNrInstrExec += 1;
      this->StatRun += 1;
      ret = thread;
      break;
    }
    if (is_blocked)
      this->StatDma += 1;
    else
      this->StatEtc += 1;
  }

  if (this->CrCurInstr == NULL) {
    RvInstr *instruction_ = this->PpReadyInstr;
    long id = this->PpReadyId;
    this->PpReadyInstr = NULL;
    if (instruction_ != (RvInstr *)1) {
      this->CrCurInstr = instruction_;
      this->CrCurId = id;
    }
  }

  if (DmmMramTimingCanPop(&this->MramTiming)) {
    long thread_id = DmmMramTimingPop(&this->MramTiming);
    // Unblock thread when DMA completes
    this->Csr[31] &= ~(1 << thread_id); // Clear blocked bit
  }

  servePipeline(this);
  serveCycleRule(this);
  return ret;
}
