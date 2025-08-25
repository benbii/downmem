#include "downmem.h"
#include <stdio.h>

static void servePipeline(DmmTiming* this) {
  // If there's space in pipeline (i.e. not full) push the incoming instruction
  // The incoming instruction may not be set by scheduler and therefore 0x1
  if (((this->PpQRear - this->PpQFrt) & 15) < NrPipelineStage - 1) {
    this->PpInsideInstrs[this->PpQRear] = this->PpInInstr;
    this->PpInsideIds[this->PpQRear] = this->PpInId;
    this->PpQRear = (this->PpQRear + 1) & 15;
    this->PpInInstr = (DmmInstr*)1;
  }
  // If no instruction is ready, pop from the queue
  if (this->PpReadyInstr == NULL) {
    DmmInstr* instr = this->PpInsideInstrs[this->PpQFrt];
    long id = this->PpInsideIds[this->PpQFrt];
    this->PpQFrt = (this->PpQFrt + 1) & 15;
    this->PpReadyInstr = instr;
    this->PpReadyId = id;
    // printf(" pop");
  }
}

static void serveCycleRule(DmmTiming* this) {
  if (this->CrCurInstr != NULL && this->CrPrevInstr == NULL) {
    DmmInstr* instr = this->CrCurInstr;
    this->CrCurInstr = NULL;
    long thread_id = this->CrCurId;
    DmmOpcode op = instr->Opcode;

    uint64_t curRead;
    if (op == DIV_STEP || op == MUL_STEP || op == SD ||
        op == MOVD || op == SWAPD) {
      curRead = (1ull << instr->RegA) | (3ull << instr->RegB);
    } else {
      curRead = (1ull << instr->RegA) | (1ull << instr->RegB);
    }
    // printf(" r %ld %lx %x", thread_id, curRead & 0xffffff,
    //        this->CrPrevWriteRegSets[thread_id] & 0xffffff);

    curRead |= this->CrPrevWriteRegSets[thread_id];
    int even_counter = __builtin_popcount(curRead & 0x555555);
    int odd_counter = __builtin_popcount(curRead & 0xaaaaaa);
    this->CrExtraCycleLeft = even_counter / 2 + odd_counter / 2;
    this->CrPrevInstr = instr;
    this->CrPrevId = thread_id;
    this->StatCycleRule += 1;
  }

  if (this->CrPrevInstr != NULL && this->CrExtraCycleLeft <= 0) {
    DmmInstr* instr = this->CrPrevInstr;
    this->CrPrevInstr = NULL;
    long thread_id = this->CrPrevId;
    DmmOpcode op = instr->Opcode;
    _Static_assert(LD + 1 == SOpcodeStart, "please dude");
    if (op >= LD) {
      this->CrPrevWriteRegSets[thread_id] = (3ull << instr->RegC);
    } else {
      this->CrPrevWriteRegSets[thread_id] = (1ull << instr->RegC);
    }
    // printf(" w %ld %x", thread_id,
    //        this->CrPrevWriteRegSets[thread_id] & 0xffffff);
  }
  this->CrExtraCycleLeft -= 1;
}

void DmmTimingInit(DmmTiming *t, DmmInstr *iram, size_t memFreq,
                   size_t logicFreq) {
  memset(t, 0, sizeof(DmmTiming));
  t->Iram = iram;
  for (long i = 0; i < MaxNumTasklets; i++) {
    DmmTletInit(&t->Threads[i], i);
    t->lastPc[i] = IramNrInstr - 1;
  }
  t->FreqRatio = (double)memFreq / (double)logicFreq;
  DmmMramTimingInit(&t->MramTiming);

  // Push dummy entries to simulate initial pipeline stages
  for (size_t i = 0; i < NrPipelineStage - 1; i++) {
    t->PpInsideInstrs[i] = (DmmInstr*)1;
    t->PpInsideIds[i] = 0;
  }
  t->PpQFrt = 0;
  t->PpQRear = NrPipelineStage - 1;
  t->PpInInstr = (DmmInstr*)1;
  t->PpReadyInstr = (DmmInstr*)1;
}

DmmTlet* DmmTimingCycle(DmmTiming* this, size_t nrTasklets) {
  long num_memory_cycles = (long)(
    this->FreqRatio * (double)this->StatNrCycle -
    this->FreqRatio * (double)(this->StatNrCycle - 1));
  for (long i = 0; i < num_memory_cycles; i++) {
    DmmMramTimingCycle(&this->MramTiming);
  }
  this->StatNrCycle++;
  // printf("\nc%ld ", this->StatNrCycle);
  DmmTlet* ret = NULL;

  if (this->PpInInstr != (DmmInstr*)1 || this->CrCurInstr != NULL) {
    this->StatNrRfHazard += 1;
  } else {
    bool is_blocked = false;
    for (long i = 0; i < nrTasklets; i++) {
      DmmTlet* thread = &this->Threads[this->lastIssue];
      this->lastIssue++;
      if (this->lastIssue == nrTasklets) { this->lastIssue = 0; }
      if (this->lastRunAt[this->lastIssue] + NrRevolveCycle > this->StatNrCycle) {
        continue;
      }
      if (thread->State != RUNNABLE) {
        is_blocked = thread->State == BLOCK;
        continue;
      }

      size_t pc = (thread->Pc & IramMask) / IramDataByte;
      DmmInstr* instr = &this->Iram[pc];
      this->PpInInstr = instr;
      this->PpInId = thread->Id;
      _Static_assert(SDMA == 2 && LDMAI == 1 && LDMA == 0, "Please dude");
      if (instr->Opcode <= SDMA) {
        __auto_type vc = thread->Regs[instr->RegA];
        __auto_type ad = (thread->Regs[instr->RegB] & 0xfffffff8);
        __auto_type sz = (1 + instr->ImmA + (vc >> 24) & 0xff) << 3;
				// printf("mem %d %d %d ", ad, sz, thread->Id);
        DmmMramTimingPush(&this->MramTiming, ad, sz, thread->Id);
        thread->State = BLOCK;
      }

      // printf("t%d %s %d %d %d", thread->Id, DmmOpStr[instr->Opcode],
      //        instr->RegC, instr->RegA, instr->RegB);
      this->StatTsc[this->lastPc[this->lastIssue]] +=
        this->StatNrCycle - this->lastRunAt[this->lastIssue];
      this->lastPc[this->lastIssue] = pc;
      this->lastRunAt[this->lastIssue] = this->StatNrCycle;
      this->StatNrInstrExec += 1;
      this->StatRun += 1;
      ret = thread;
      break;
    }
    if (is_blocked) { this->StatDma += 1; } else { this->StatEtc += 1; }
  }

  if (this->CrCurInstr == NULL) {
    DmmInstr* instruction_ = this->PpReadyInstr;
    long id = this->PpReadyId;
    this->PpReadyInstr = NULL;
    if (instruction_ != (DmmInstr*)1) {
      this->CrCurInstr = instruction_;
      this->CrCurId = id;
    }
  }

  if (DmmMramTimingCanPop(&this->MramTiming)) {
    this->Threads[DmmMramTimingPop(&this->MramTiming)].State = RUNNABLE;
  }
  servePipeline(this);
  serveCycleRule(this);
  return ret;
}

