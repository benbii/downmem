#define PCRE2_CODE_UNIT_WIDTH 8
#include "downmem.h"
#include <assert.h>
#include <byteswap.h>
#include <pcre2.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/mman.h>

// Regex compiled patterns
static pcre2_code *instrRe, *symRe, *dataRe;
static pcre2_match_data *instrMat, *symMat, *dataMat;
static void __attribute__((constructor)) regex_init() {
  int errcode;
  PCRE2_SIZE erroffset;
  instrRe = pcre2_compile(
      (PCRE2_SPTR) "^[0-9a-f]{8}: (?:[0-9a-f]{2} ){8} {5}\\t([\\w.]+) "
                   "?([\\w-]+)?,? ?([\\w-]+)?,? ?([\\w-]+)?,? ?([\\w-]+)?,? "
                   "?([\\w-]+)?,? ?([\\w-]+)?",
      PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
  symRe = pcre2_compile(
      (PCRE2_SPTR) "^([0-9a-f]{8}) [lg] +[dfFO]* \\S+\\t[0-9a-f]{8} (\\S+)$",
      PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
  dataRe = pcre2_compile((PCRE2_SPTR)
      "^ ([0-9a-f]+) ([0-9a-f]{8}) ([0-9a-f]{8})? "
      "([0-9a-f]{8})? ([0-9a-f]{8})?  +.{8,}$",
      PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
  if (!instrRe || !symRe || !dataRe)
    exit(fprintf(stderr, "Failed to compile regex\n"));
  instrMat = pcre2_match_data_create_from_pattern(instrRe, NULL);
  symMat = pcre2_match_data_create_from_pattern(symRe, NULL);
  dataMat = pcre2_match_data_create_from_pattern(dataRe, NULL);
}

void DmmPrgInit(DmmPrg* p) {
  // coreId = numa_node_of_cpu(coreId);
  p->WMAram = mmap(NULL, WMAINrByte, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (p->WMAram == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);
  }
  if (madvise(p->WMAram, WMAINrByte, MADV_HUGEPAGE) != 0)
    perror("madvise");
  p->Iram = (DmmInstr*)(p->WMAram + WramSize + MramSize + AtomicSize);
  // if (coreId >= 0) {
  //   coreId = 1 << coreId;
  //   if (0 != mbind(p->WMAram, WramSize + MramSize + AtomicSize, MPOL_BIND,
  //                  &coreId, sizeof(coreId), 0))
  //     perror("mbind");
  // }
}
void DmmPrgFini(DmmPrg* p) {
  munmap(p->WMAram, WMAINrByte);
}

// -- OBJDUMP parsing related functions --
// helpers for instruction parsing
static uint32_t parseImmediate(const char *imm, PCRE2_SIZE sz,
                               DmmMap symbols);
static uint8_t parseRegister(const char* reg, PCRE2_SIZE sz);
static DmmInstr stores(const char* fields, const PCRE2_SIZE *ovector,
                       size_t nrFields, DmmMap symbols);
static DmmInstr subs(const char* fields, const PCRE2_SIZE *ovector,
                       size_t nrFields, DmmMap symbols);
static DmmInstr jumps(const char* fields, const PCRE2_SIZE *ovector,
                      size_t nrFields, DmmMap symbols);
static DmmInstr allothers(const char* fields, const PCRE2_SIZE *ovector,
                          size_t nrFields, DmmMap symbols);

// ObjdLnToInstr turns an objdump line into an Instr struct.
DmmInstr ObjdLnToInstr(const char* objdumpLine, size_t sz, DmmMap symbols) {
  int rc = pcre2_match(instrRe, (PCRE2_SPTR)objdumpLine, sz, 0, 0, instrMat, NULL);
  if (rc < 2)
    return (DmmInstr){.Opcode = MapNoInt};
  PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(instrMat) + 2, ov0 = ovector[0];
  if (objdumpLine[ov0] == 'j') {
    return jumps(objdumpLine, ovector, rc-1, symbols);
  } else if (objdumpLine[ov0] == 's') {
    if (ovector[1] - ov0 == 2)
      return stores(objdumpLine, ovector, rc-1, symbols);
    else if (objdumpLine[ov0 + 1] == 'u')
      return subs(objdumpLine, ovector, rc-1, symbols);
  }
  return allothers(objdumpLine, ovector, rc-1, symbols);
}

ObjdLnToDatRet ObjdLnToDat(const char* objdumpLine, size_t sz) {
  ObjdLnToDatRet ret = {.NrDat=0};
  int rc = pcre2_match(dataRe, (PCRE2_SPTR)objdumpLine, sz, 0, 0, dataMat, NULL);
  if (rc <= 0)
    return ret;  // No match, NrDat remains 0
  PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(dataMat);
  char buf[9] = {[8] = '\0'}; // hold hex substrings (max 8 chars + null)
  ret.NrDat = rc - 2;  // Ignore full match + first group (address)

  for (uint32_t i = 0; i < ret.NrDat; i++) {
    // Group 2, 3, 4, 5
    size_t start = ovector[(i+2)*2];
    strncpy(buf, objdumpLine + start, 8);
    ret.Dat[i] = bswap_32((uint32_t)strtoul(buf, NULL, 16));
  }
  // Address (first group)
  ret.Addr = (uint32_t)strtoul(objdumpLine + ovector[2], NULL, 16);
  return ret;
}

// ObjdLnToSym tries parsing an objdump line of size *outBufSz into a (symbol
// name, SymbAddr) pair. SymbAddr is returned, and symbol name is saved into
// outBuf. If the line does not match, it sets *outBufSz to 0.
DmmSymAddr ObjdLnToSym(const char *objdumpLine, size_t linesz, char *outBuf,
                       size_t* outBufSz) {
  int rc = pcre2_match(symRe, (PCRE2_SPTR)objdumpLine, linesz, 0, 0, symMat, NULL);
  if (rc != 3) { // We expect exactly 3 matches (full, addr, name)
    *outBufSz = 0;
    return MapNoInt; // No match
  }

  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(symMat);
  DmmSymAddr outAddr = strtoul(objdumpLine + ovector[2], NULL, 16);
  // Extract symbol name (second captured group)
  size_t nameLen = ovector[5] - ovector[4];
  if (nameLen >= *outBufSz)
    nameLen = *outBufSz - 1;
  strncpy(outBuf, objdumpLine + ovector[4], nameLen);
  outBuf[nameLen] = '\0';
  *outBufSz = nameLen;
  return outAddr; // Success
}

// Store instructions. The expected order of fields is:
//	[regA?] [immA?] [regB?] [immB?]
static DmmInstr stores(const char* fields, const PCRE2_SIZE *ovector,
                       size_t nrFields, DmmMap symbols) {
  // Extract opcode
  const char* opcode = fields + ovector[0];
  size_t opcodeLen = ovector[1] - ovector[0];
  DmmInstr instr = {
    .Opcode = DmmMapFetch(DmmStrToOpcode, opcode, opcodeLen),
    .Cond = NoCond,
    .RegC = NullReg, .RegA = ZeroReg, .RegB = ZeroReg,
    .ImmA = ZeroImm, .ImmB = ZeroImm,
  };
  // Lookup opcode in stringToOpcode map
  if (instr.Opcode == MapNoInt)
    return instr;
  size_t curAt = 1;
  // Helper to access field `curAt` safely
#define FIELD_AT(n)  fields + ovector[2*n], ovector[2*n+1] - ovector[2*n]

  // 1. Parse regA
  if (curAt < nrFields) {
    uint8_t reg = parseRegister(FIELD_AT(curAt));
    if (reg != badReg) {
      instr.RegA = reg;
      curAt++;
    }
  }
  // 2. Parse immA
  if (curAt < nrFields) {
    uint32_t immA = parseImmediate(FIELD_AT(curAt), symbols);
    if (immA != badImm) {
      instr.ImmA = immA;
      curAt++;
    }
  }
  // 3. Parse regB
  if (curAt < nrFields) {
    uint8_t reg = parseRegister(FIELD_AT(curAt));
    if (reg != badReg) {
      instr.RegB = reg;
      curAt++;
    }
  }
  // 4. Parse immB
  if (curAt < nrFields) {
    uint32_t immB = parseImmediate(FIELD_AT(curAt), symbols);
    if (immB != badImm) {
      instr.ImmB = immB;
      curAt++;
    }
  }
  return instr;
}

static DmmInstr subs(const char* fields, const PCRE2_SIZE *ovector,
                       size_t nrFields, DmmMap symbols) {
  // Extract opcode
  const char* opcode = fields + ovector[0];
  size_t opcodeLen = ovector[1] - ovector[0];
  DmmInstr instr = {
    .Opcode = DmmMapFetch(DmmStrToOpcode, opcode, opcodeLen),
  // 1. regC is required
    .RegC = parseRegister(FIELD_AT(1)),
    .RegA = ZeroReg, .RegB = ZeroReg,
    .ImmA = ZeroImm, .ImmB = ZeroImm, .Cond = NoCond,
  };
  // Lookup opcode in stringToOpcode map
  if (instr.Opcode == MapNoInt)
    return instr;

  if (instr.RegC == ZeroReg)
    instr.RegC = NullReg;
  // 2. operand 1, either reg or immediate, also required
  int32_t myimm = (int32_t)parseImmediate(FIELD_AT(2), symbols);
  if (myimm != badImm) instr.ImmA = -myimm; // ra - rb - immA
  else instr.RegA = parseRegister(FIELD_AT(2));
  // 3. operand 2, parsed similarly as operand1
  myimm = (int32_t)parseImmediate(FIELD_AT(3), symbols);
  if (myimm != badImm) instr.ImmA = myimm;
  else instr.RegB = parseRegister(FIELD_AT(3));
  assert(nrFields >= 4 && instr.ImmA != badImm);
  assert(badReg != instr.RegC && badReg != instr.RegA && badReg != instr.RegB);

  // 4. optional condition and pc
  size_t curAt = 4;
  if (curAt < nrFields) {
    DmmCc cond = DmmMapFetch(DmmStrToCc, FIELD_AT(curAt));
    if (cond != MapNoInt) {
      instr.Cond = cond;
      curAt++;
    }
  }
  if (curAt < nrFields) {
    uint32_t immB = parseImmediate(FIELD_AT(curAt), symbols);
    if (immB != badImm)
      instr.ImmB = immB;
  }
  return instr;
}

static DmmInstr jumps(const char* fields, const PCRE2_SIZE *ovector,
                      size_t nrFields, DmmMap symbols) {
  DmmInstr instr = {
    .Opcode = JMP, .Cond = NoCond,
    .RegA = ZeroReg, .RegB = ZeroReg, .RegC = NullReg,
    .ImmA = ZeroImm, .ImmB = ZeroImm,
  };
  size_t l = nrFields - 1; // Length of "non-opcode" fields

  // `jump` executes differently than conditional jumps
  if (fields[1 + ovector[0]] == 'u') {
    instr.Opcode = CALL;
    // Parse first field (fields[1]) as register or immediate
    uint8_t ra = parseRegister(FIELD_AT(1));
    if (ra == badReg) {
      instr.ImmA = parseImmediate(FIELD_AT(1), symbols);
      assert(instr.ImmA != badImm);
    } else {
      instr.RegA = ra;
      // If there's a second field (fields[2]), parse it as ImmA
      if (l >= 2) {
        instr.ImmA = parseImmediate(FIELD_AT(2), symbols);
        assert(instr.ImmA != badImm);
        // UPMEM 2025 llvm-objdump is bugged: offset display is wrong in `jump
        // ra off` instructions
        instr.ImmA = (instr.ImmA << 3) + IramMask + 1;
      }
    }
    return instr;
  }

  // Handle conditional jumps: First parse condition from jmpcode[1:] (this is
  // the "condition" suffix of the opcode itself).
  // See what the macro expands into [Chuckle]
  instr.Cond = DmmMapFetch(DmmStrToJcc, 1 + FIELD_AT(0) - 1);
  // Last field (fields[l]) is always ImmB
  instr.ImmB = parseImmediate(FIELD_AT(l), symbols);
  assert(instr.ImmB != MapNoInt);
  // If at least 2 fields, parse first as RegA
  if (l >= 2) {
    instr.RegA = parseRegister(FIELD_AT(1));
    assert(instr.RegA != badReg);
    // If at least 3 fields, parse second as RegB or ImmA
    if (l >= 3) {
      uint8_t rb = parseRegister(FIELD_AT(2));
      if (rb == badReg) {
        instr.ImmA = parseImmediate(FIELD_AT(2), symbols);
        assert(instr.ImmA != badImm);
      } else instr.RegB = rb;
    }
  }
  return instr;
}

static DmmInstr allothers(const char* fields, const PCRE2_SIZE *ovector,
                          size_t nrFields, DmmMap symbols) {
  const char* opcode = fields + ovector[0];
  size_t opcodeLen = ovector[1] - ovector[0];
  DmmInstr instr = {
    .RegC = NullReg, .RegA = ZeroReg, .RegB = ZeroReg,
    .ImmA = ZeroImm, .ImmB = ZeroImm,
    .Cond = NoCond, .Opcode = DmmMapFetch(DmmStrToOpcode, opcode, opcodeLen)
  };
  if (instr.Opcode == MapNoInt)
    exit(fprintf(stderr, "Unrecognized opcode %.*s\n", (int)opcodeLen, opcode));
  size_t curAt = 1;

  // 0. Parse RegC (destination register)
  if (curAt < nrFields) {
    uint8_t reg = parseRegister(FIELD_AT(curAt));
    if (reg != badReg) {
      if (reg != ZeroReg || DmmOpWbMode[instr.Opcode] == noWb)
        instr.RegC = reg;
      curAt++;
    }
  }
  // 1. Parse RegA (first source register)
  if (curAt < nrFields) {
    uint8_t reg = parseRegister(FIELD_AT(curAt));
    if (reg != badReg) {
      instr.RegA = reg;
      curAt++;
    }
  }
  // 2. Parse RegB (second source register)
  if (curAt < nrFields) {
    uint8_t reg = parseRegister(FIELD_AT(curAt));
    if (reg != badReg) {
      instr.RegB = reg;
      curAt++;
    }
  }

  // 3. Parse immA (first immediate)
  if (curAt < nrFields) {
    uint32_t immA = parseImmediate(FIELD_AT(curAt), symbols);
    if (immA != badImm) {
      instr.ImmA = immA;
      curAt++;
    }
  }
  // 4. Parse condition (stringToCondition lookup)
  if (curAt < nrFields) {
    DmmCc cond = DmmMapFetch(DmmStrToCc, FIELD_AT(curAt));
    if (cond != MapNoInt) {
      instr.Cond = cond;
      curAt++;
    }
  }
  // 5. Parse immB (second immediate)
  if (curAt < nrFields) {
    uint32_t immB = parseImmediate(FIELD_AT(curAt), symbols);
    if (immB != badImm)
      instr.ImmB = immB;
  }

  // these instructions read the first register (RegC)
  if (instr.Opcode == ACQUIRE || instr.Opcode == RELEASE ||
      instr.Opcode == STOP || instr.Opcode == BOOT || instr.Opcode == RESUME) {
    instr.RegA = instr.RegC;
    instr.RegC = NullReg;
  }
  // these instructions read the first and second registers
  if (instr.Opcode == LDMA || instr.Opcode == SDMA || instr.Opcode == LDMAI) {
    instr.RegB = instr.RegA;
    instr.RegA = instr.RegC;
    instr.RegC = NullReg;
  }
  return instr;
}

static uint32_t parseImmediate(const char *imm, PCRE2_SIZE sz,
                               DmmMap symbols) {
  char* endptr;
  long val = strtol(imm, &endptr, 0);  // Handles decimal & hex (0x)
  if (endptr != imm) return (uint32_t)val;
  // Look up in symbol map if not a plain number
  return DmmMapFetch(symbols, (void*)imm, sz);
}

static uint8_t parseRegister(const char* reg, PCRE2_SIZE sz) {
  if (reg[0] == 'r' || reg[0] == 'd') {
    char* endptr;
    size_t num = strtoul(reg+1, &endptr, 10);
    if (endptr != reg+1 && num < 32)
      return (uint8_t)num;
  }
  if (sz == 4 && memcmp(reg, "zero", 4) == 0) return 24; // ZeroReg
  if (sz == 4 && memcmp(reg, "lneg", 4) == 0) return 26;
  if (sz == 4 && memcmp(reg, "mneg", 4) == 0) return 27;
  if (sz == 3 && memcmp(reg, "one", 3) == 0) return 25;
  if (sz == 3 && memcmp(reg, "id2", 3) == 0) return 29;
  if (sz == 3 && memcmp(reg, "id4", 3) == 0) return 30;
  if (sz == 3 && memcmp(reg, "id8", 3) == 0) return 31;
  if (sz == 2 && memcmp(reg, "id", 2) == 0) return 28;
  return badReg;  // Default case
}

size_t DmmPrgReadObjdump(DmmPrg *p, const char *filename, DmmMap symbols,
                         bool paged[WMAINrPage]) {
  if (paged != NULL)
    memset(paged, 0, WMAINrPage);
  FILE* scanner = fopen(filename, "rb");
  if (scanner == NULL) return 0;
  // p->Iram already allocated
  size_t iramAt = 0;

  while (!feof(scanner)) {
    char line[256], outBuf[128];
    fgets(line, 256, scanner);
    size_t lineSz = strlen(line), outBufSz = 128;
    // If the line is a symbol definition, add it to the symbols table.
    DmmSymAddr symAddr = ObjdLnToSym(line, lineSz, outBuf, &outBufSz);
    if (outBufSz != 0) {
      assert(outBufSz < 128 && "Symbol length >=128 not supported!");
      // TODO: symbol names leaked!!!
      char* n = strndup(outBuf, outBufSz);
      DmmMapAssign(symbols, n, outBufSz, symAddr);
      continue;
    }

    // Otherwise, try to parse the line as an instruction.
    DmmInstr instr = ObjdLnToInstr(line, lineSz, symbols);
    if (instr.Opcode != MapNoInt) {
      p->Iram[iramAt++] = instr;
      if (iramAt >= IramNrInstr) {
        fputs("UPMEM program can only hold 4096 instructions\n", stderr);
        return 0;
      }
      continue;
    }

    // Then try parse the line as WRAM / MRAM data
    ObjdLnToDatRet dat = ObjdLnToDat(line, lineSz);
    if (dat.NrDat == 0) {
      continue;
    } else if (dat.Addr > 0xf0000000) { // Atomic
      dat.Addr = WramSize + MramSize + (dat.Addr & AtomicMask);
    } else if (dat.Addr >= MramBegin) { // MRAM
      dat.Addr = WramSize + (dat.Addr & WramMask);
    }
    if (paged != NULL) {
      paged[dat.Addr / 4096] = true;
      paged[(dat.Addr + 16) / 4096] = true;
    }
    uint32_t *dest = (uint32_t*)(p->WMAram + dat.Addr);
    switch (dat.NrDat) {
    default: __builtin_unreachable();
    case 4: dest[3] = dat.Dat[3];
    case 3: dest[2] = dat.Dat[2];
    case 2: dest[1] = dat.Dat[1];
    case 1: dest[0] = dat.Dat[0];
    }
  }
  return iramAt;
}
