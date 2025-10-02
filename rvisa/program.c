#include "dmminternal.h"
#include <assert.h>
#include <byteswap.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __DMM_NUMA
#include <numa.h>
#include <numaif.h>
#endif
#include <libelf.h>
#include <gelf.h>

// --- RISC-V Instruction Format Types ---
typedef enum {
  RV_R_TYPE,    // Register-register operations
  RV_I_TYPE,    // Immediate operations
  RV_S_TYPE,    // Store operations
  RV_B_TYPE,    // Branch operations
  RV_U_TYPE,    // Upper immediate operations
  RV_J_TYPE     // Jump operations
} RvInstrType;

// --- RISC-V Hardware Opcodes (bits 6:0) ---
#define RV_LOAD      0x03
#define RV_MISC_MEM  0x0F
#define RV_OP_IMM    0x13
#define RV_AUIPC     0x17
#define RV_STORE     0x23
#define RV_OP        0x33
#define RV_LUI       0x37
#define RV_BRANCH    0x63
#define RV_JALR      0x67
#define RV_JAL       0x6F
#define RV_SYSTEM    0x73
#define RV_CUSTOM_0  0x0B

// --- Bit field extraction macros ---
#define OPCODE(x)    ((x) & 0x7F)
#define RD(x)        (((x) >> 7) & 0x1F)
#define FUNCT3(x)    (((x) >> 12) & 0x7)
#define RS1(x)       (((x) >> 15) & 0x1F)
#define RS2(x)       (((x) >> 20) & 0x1F)
#define FUNCT2(x)    (((x) >> 25) & 0x3)   // For R4 format
#define SIZE_FIELD(x) (((x) >> 22) & 0x1F)  // Custom size field for DMA instructions [26:22]
#define FUNCT7(x)    (((x) >> 25) & 0x7F)

// --- Immediate extraction functions ---
static int32_t __immi(uint32_t instr) {
  return (int32_t)(instr) >> 20;  // Sign extend bits [31:20]
}

static int32_t __imms(uint32_t instr) {
  int32_t imm = ((instr >> 7) & 0x1F) | ((instr >> 20) & 0xFE0);
  return (imm << 20) >> 20;  // Sign extend 12 bits
}

static int32_t __immb(uint32_t instr) {
  int32_t imm = ((instr >> 7) & 0x1E) |    // imm[4:1]
                ((instr >> 20) & 0x7E0) |  // imm[10:5]
                ((instr << 4) & 0x800) |   // imm[11]
                ((instr >> 19) & 0x1000);  // imm[12]
  return (imm << 19) >> 19;  // Sign extend 13 bits
}

static int32_t __immu(uint32_t instr) {
  return instr & 0xFFFFF000;  // Upper 20 bits
}

static int32_t __immj(uint32_t instr) {
  int32_t imm = ((instr >> 20) & 0x7FE) |   // imm[10:1]
                ((instr >> 9) & 0x800) |    // imm[11]
                (instr & 0xFF000) |         // imm[19:12]
                ((instr >> 11) & 0x100000); // imm[20]
  return (imm << 11) >> 11;  // Sign extend 21 bits
}

static RvInstr rvdecode(uint32_t encoded) {
  RvInstr instr = {0};
  uint32_t opcode = OPCODE(encoded);
  uint32_t funct3 = FUNCT3(encoded);
  uint32_t funct7 = FUNCT7(encoded);
  instr.rd = RD(encoded);
  instr.rs1 = RS1(encoded);
  instr.rs2 = RS2(encoded);

  switch (opcode) {
    case RV_LUI: instr.Opcode = LUI; instr.imm = __immu(encoded); break;
    case RV_AUIPC: instr.Opcode = AUIPC; instr.imm = __immu(encoded); break;
    case RV_JAL: instr.Opcode = JAL; instr.imm = __immj(encoded); break;
    case RV_JALR: instr.Opcode = JALR; instr.imm = __immi(encoded); break;
    case RV_BRANCH:
      instr.imm = __immb(encoded);
      switch (funct3) {
        case 0x0: instr.Opcode = BEQ; break;
        case 0x1: instr.Opcode = BNE; break;
        case 0x4: instr.Opcode = BLT; break;
        case 0x5: instr.Opcode = BGE; break;
        case 0x6: instr.Opcode = BLTU; break;
        case 0x7: instr.Opcode = BGEU; break;
        default: goto die;
      }
      break;
      
    case RV_LOAD:
      instr.imm = __immi(encoded);
      switch (funct3) {
        case 0x0: instr.Opcode = LBr; break;
        case 0x1: instr.Opcode = LHr; break;
        case 0x2: instr.Opcode = LWr; break;
        case 0x4: instr.Opcode = LBUr; break;
        case 0x5: instr.Opcode = LHUr; break;
        default: goto die;
      }
      break;
      
    case RV_STORE:
      instr.imm = __imms(encoded);
      switch (funct3) {
        case 0x0: instr.Opcode = SBr; break;
        case 0x1: instr.Opcode = SHr; break;
        case 0x2: instr.Opcode = SWr; break;
        default: goto die;
      }
      break;
      
    case RV_OP_IMM:
      instr.imm = __immi(encoded);
      switch (funct3) {
        case 0x0: instr.Opcode = ADDI; break;
        case 0x2: instr.Opcode = SLTI; break;
        case 0x3: instr.Opcode = SLTIU; break;
        case 0x4: instr.Opcode = XORI; break;
        case 0x6: instr.Opcode = ORI; break;
        case 0x7: instr.Opcode = ANDI; break;
        case 0x1: 
          // SLLI and Zbb unary bit manipulation instructions
          if (funct7 == 0x00) instr.Opcode = SLLI;
          else if (funct7 == 0x30) {
            // Zbb unary instructions using OP_IMM encoding
            if (instr.rs2 == 0) instr.Opcode = CLZr;       // clz
            else if (instr.rs2 == 1) instr.Opcode = CTZ;  // ctz  
            else if (instr.rs2 == 2) instr.Opcode = CPOP; // cpop
            else goto die;
          }
          else goto die;
          break;
        case 0x5:
          if (funct7 == 0x00) instr.Opcode = SRLI;
          else if (funct7 == 0x20) instr.Opcode = SRAI;
          else if (funct7 == 0x30) instr.Opcode = RORI; // RV32B Zbb rotate right immediate
          else goto die;
          break;
        default: goto die;
      }
      break;
      
    case RV_OP:
      switch (funct7) {
        case 0x00:
          // Standard RV32I arithmetic
          switch (funct3) {
            case 0x0: instr.Opcode = ADDr; break;
            case 0x1: instr.Opcode = SLL; break;
            case 0x2: instr.Opcode = SLT; break;
            case 0x3: instr.Opcode = SLTU; break;
            case 0x4: instr.Opcode = XORr; break;
            case 0x5: instr.Opcode = SRL; break;
            case 0x6: instr.Opcode = ORr; break;
            case 0x7: instr.Opcode = ANDr; break;
            default: goto die;
          }
          break;
        case 0x20:
          // SUB, SRA, and Zbb ANDN instruction
          switch (funct3) {
            case 0x0: instr.Opcode = SUBr; break;
            case 0x5: instr.Opcode = SRA; break;
            case 0x7: instr.Opcode = ANDNr; break;     // andn
            default: goto die;
          }
          break;
        case 0x01:
          // RV32M: Multiplication and Division
          switch (funct3) {
            case 0x0: instr.Opcode = MUL; break;     // MUL
            case 0x1: instr.Opcode = MULH; break;    // MULH
            case 0x2: instr.Opcode = MULHSU; break;  // MULHSU
            case 0x3: instr.Opcode = MULHU; break;   // MULHU
            case 0x4: instr.Opcode = DIV; break;     // DIV
            case 0x5: instr.Opcode = DIVU; break;    // DIVU
            case 0x6: instr.Opcode = REM; break;     // REM
            case 0x7: instr.Opcode = REMU; break;    // REMU
            default: goto die;
          }
          break;
        case 0x05:
          // RV32B Zbb: Basic bit manipulation (min/max)
          switch (funct3) {
            case 0x4: instr.Opcode = MIN; break;     // MIN
            case 0x5: instr.Opcode = MINU; break;    // MINU
            case 0x6: instr.Opcode = MAXr; break;     // MAX
            case 0x7: instr.Opcode = MAXU; break;    // MAXU
            default: goto die;
          }
          break;
        case 0x30:
          // RV32B Zbb: Bit manipulation instructions
          switch (funct3) {
            case 0x1: 
              // CLZ, CTZ, CPOP (unary operations using rs2 to distinguish)
              if (instr.rs2 == 0) instr.Opcode = CLZr;       // clz
              else if (instr.rs2 == 1) instr.Opcode = CTZ;  // ctz
              else if (instr.rs2 == 2) instr.Opcode = CPOP; // cpop
              else instr.Opcode = ROLr;                      // rol (binary operation, default)
              break;
            case 0x4: instr.Opcode = SEXT_B; break;       // sext.b
            case 0x5: 
              if (instr.rs2 == 5) instr.Opcode = SEXT_H;    // sext.h (unary)
              else if (instr.rs2 == 4) instr.Opcode = ZEXT_H; // zext.h (unary)
              else instr.Opcode = RORr;                      // ror (binary operation)
              break;
            default: goto die;
          }
          break;
        case 0x40:
          // RV32B Zbb: More bitwise operations
          switch (funct3) {
            case 0x6: instr.Opcode = ORNr; break;      // orn
            case 0x4: instr.Opcode = XNOR; break;     // xnor
            default: goto die;
          }
          break;
        default: goto die;
      }
      break;
      
    case RV_MISC_MEM:
      if (funct3 == 0x0) instr.Opcode = FENCE;
      else goto die;
      
    case RV_SYSTEM:
      if (encoded == 0x73) instr.Opcode = ECALL;
      else if (encoded == 0x100073) instr.Opcode = EBREAK;
      else {
        // CSR instructions
        instr.imm = (encoded >> 20) & 0xFFF;  // CSR address in bits [31:20]
        switch (funct3) {
          case 0x1: instr.Opcode = CSRRW; break;
          case 0x2: instr.Opcode = CSRRS; break;
          case 0x3: instr.Opcode = CSRRC; break;
          case 0x5: instr.Opcode = CSRRWI; break;
          case 0x6: instr.Opcode = CSRRSI; break;
          case 0x7: instr.Opcode = CSRRCI; break;
          default: goto die;
        }
      }
      break;
      
    case RV_CUSTOM_0:
      // Custom DPU instructions with special encoding
      instr.rs2 = SIZE_FIELD(encoded);
      switch (funct3) {
        case 0x0: instr.Opcode = MYID; break;
        // LDMRAM: wramAddr=rs1[19:15], mramAddr=rd[11:7], size=custom[26:22]
        case 0x1: instr.Opcode = LDMRAM; break;
        // SDMRAM: wramAddr=rs1[19:15], mramAddr=rd[11:7], size=custom[26:22]
        case 0x2: instr.Opcode = SDMRAM; break;
        default: goto die;
      }
      break;
      
    default: goto die;
  }

  uint8_t needRw = RvNeedRw[instr.Opcode];
  if (!(needRw & 4)) instr.rd = 0; // excluded from timing calculation
  if ((needRw & 3) < 2) instr.rs2 = 0;
  if ((needRw & 3) < 1) instr.rs1 = 0;
  return instr;
die:
  exit(fprintf(stderr, "Unknown instr %x\n", encoded));
}

void RvPrgInit(RvPrg* p, int numaNode) {
  p->WMAram = mmap(NULL, WMAINrByteR, PROT_READ | PROT_WRITE,
                  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (p->WMAram == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);
  }
  p->Iram = (RvInstr*)(p->WMAram + WramSizeR + MramSizeR);

  if (madvise(p->WMAram, WMAINrByteR, MADV_HUGEPAGE) != 0)
    perror("madvise");
  (void)numaNode;
#ifdef __DMM_NUMA
  // Bind WMAram to specified NUMA node
  if (numaNode >= 0) {
    struct bitmask *mask = numa_allocate_nodemask();
    numa_bitmask_setbit(mask, numaNode);
    long ret = mbind(p->WMAram, WMAINrByteR, MPOL_BIND,
                     mask->maskp, mask->size + 1, MPOL_MF_MOVE | MPOL_MF_STRICT);
    if (ret != 0)
      perror("mbind RvPrg WMAram");
    numa_free_nodemask(mask);
  }
#endif
}
void RvPrgFini(RvPrg* p) {
  if (p->WMAram != NULL)
    munmap(p->WMAram, WMAINrByteR);
}

size_t RvPrgLoadBinary(RvPrg *p, const char *filename, DmmMap symbols,
                       bool paged[WMAINrPageR]) {
  if (paged != NULL) memset(paged, 0, WMAINrPageR);
  size_t iram_count = 0;
  // open elf boilerplate
  if (elf_version(EV_CURRENT) == EV_NONE) {
    fprintf(stderr, "elf_verson failed: %s\n", elf_errmsg(-1));
    return 0;
  }
  int fd = open(filename, O_RDONLY);
  if (fd == -1) { perror("open"); return 0; }
  Elf *elf = elf_begin(fd, ELF_C_READ, NULL);
  if (!elf) {
    fprintf(stderr, "elf_begin failed: %s\n", elf_errmsg(-1));
    goto die;
  }
  GElf_Ehdr ehdr;
  if (gelf_getehdr(elf, &ehdr) == NULL) {
    fprintf(stderr, "gelf_getehdr failed: %s\n", elf_errmsg(-1));
    goto die;
  }
  Elf_Scn *scn = NULL; size_t shstrndx;
  if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
    fprintf(stderr, "elf_getshdrstrndx failed: %s\n", elf_errmsg(-1));
    goto die;
  }
  RvPrgInit(p, -1);

  while ((scn = elf_nextscn(elf, scn)) != NULL) {
    GElf_Shdr shdr;
    if (gelf_getshdr(scn, &shdr) != &shdr) continue;
    char *section_name = elf_strptr(elf, shstrndx, shdr.sh_name);
    if (!section_name) continue;
    Elf_Data *data = elf_getdata(scn, NULL);
    if (!data) continue;

    // Handle symbol table for symbol loading
    if (symbols != NULL && shdr.sh_type == SHT_SYMTAB) {
      // Get symbol string table
      Elf_Scn *str_scn = elf_getscn(elf, shdr.sh_link);
      Elf_Data *str_data = elf_getdata(str_scn, NULL);
      char *d_buf = malloc(str_data->d_size);
      assert(d_buf != NULL);
      memcpy(d_buf, str_data->d_buf, str_data->d_size);
      if (!str_data) continue;
      // Process symbols
      size_t sym_count = shdr.sh_size / shdr.sh_entsize;
      for (size_t i = 0; i < sym_count; i++) {
        GElf_Sym sym;
        if (gelf_getsym(data, i, &sym) != &sym) continue;
        char *name = d_buf + sym.st_name;
        if (name[0] != '$')
          DmmMapAssign(symbols, name, strlen(name), (uint32_t)sym.st_value);
      }
    }

    // Handle instruction sections (.boot and .text)
    if (strcmp(section_name, ".boot") == 0 || strcmp(section_name, ".text") == 0) {
      if (shdr.sh_type != SHT_PROGBITS) continue;
      uint32_t *instructions = (uint32_t*)data->d_buf;
      size_t instr_count = data->d_size / 4;
      for (size_t i = 0; i < instr_count && iram_count < IramNrInstrR; i++) {
        uint32_t raw_instr = instructions[i];
        // Convert endianness if needed
        if (ehdr.e_ident[EI_DATA] == ELFDATA2MSB)
          raw_instr = bswap_32(raw_instr);
        p->Iram[iram_count++] = rvdecode(raw_instr);
      }
    }

    // Handle WRAM data sections
    else if (strcmp(section_name, ".data") == 0 ||
             strcmp(section_name, ".sdata") == 0) {
      memcpy(p->WMAram + shdr.sh_addr, data->d_buf, data->d_size);
      // Mark WRAM pages as used
      if (paged != NULL)
        for (size_t addr = shdr.sh_addr; addr < shdr.sh_addr + data->d_size;
             addr += 4096)
          paged[addr / 4096] = true;
    }
    // Handle MRAM sections
    else if (strcmp(section_name, ".mram") == 0) {
      // MRAM starts after WRAM in our memory layout
      size_t off = WramSizeR + shdr.sh_addr - MramBeginR;
      memcpy(p->WMAram + off, data->d_buf, data->d_size);
      // Mark MRAM pages as used
      if (paged != NULL)
        for (size_t addr = off; addr < off + data->d_size; addr += 4096)
          paged[addr / 4096] = true;
    }
  }

die:
  elf_end(elf);
  close(fd);
  return iram_count;
}
