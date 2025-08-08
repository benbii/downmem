#include "downmem.h"

uint8_t DmmOpWbMode[NrOpcode] = {
  [LDMA]=noWb, [LDMAI]=noWb, [SDMA]=noWb, [MUL_STEP]=wbZf, [DIV_STEP]=noWb,
  [MOVD]=noWb, [MOVE]=wbZf, [SWAPD]=noWb, [LD]=noWb, [LW]=wbNoZf, [LBS]=wbNoZf,
  [LBU]=wbNoZf, [LHS]=wbNoZf, [LHU]=wbNoZf, [SB]=noWb, [SH]=noWb, [SW]=noWb,
  [SD]=noWb, [SB_ID]=noWb, [SH_ID]=noWb, [SW_ID]=noWb, [SD_ID]=noWb,
  [EXTSB]=wbZf, [EXTSH]=wbZf, [MUL_SL_SL]=wbZf, [MUL_SL_SH]=wbZf,
  [MUL_SH_SL]=wbZf, [MUL_SH_SH]=wbZf, [MUL_SL_UL]=wbZf, [MUL_SL_UH]=wbZf,
  [MUL_SH_UL]=wbZf, [MUL_SH_UH]=wbZf, [MUL_UL_SL]=wbZf, [MUL_UL_SH]=wbZf,
  [MUL_UH_SL]=wbZf, [MUL_UH_SH]=wbZf, [EXTUB]=wbZf, [EXTUH]=wbZf, [CLZ]=wbZf,
  [CLO]=wbZf, [CLS]=wbZf, [CAO]=wbZf, [MUL_UL_UL]=wbZf, [MUL_UL_UH]=wbZf,
  [MUL_UH_UL]=wbZf, [MUL_UH_UH]=wbZf, [JMP]=noWb, [CALL]=noWb, [ACQUIRE]=noWb,
  [RELEASE]=noWb, [STOP]=noWb, [BOOT]=noWb, [RESUME]=noWb, [CLR_RUN]=noWb,
  [TIME]=noWb, [TIME_CFG]=noWb, [NOP]=noWb, [FAULT]=noWb, [ADD]=wbZf,
  [ADDC]=wbZf, [SUB]=wbZf, [SUBC]=wbZf, [AND]=wbZf, [NAND]=wbZf, [ANDN]=wbZf,
  [OR]=wbZf, [NOR]=wbZf, [ORN]=wbZf, [XOR]=wbZf, [NXOR]=wbZf, [HASH]=wbZf,
  [SATS]=wbZf, [CMPB4]=wbZf, [ROL]=wbZf, [ROR]=wbZf, [LSL]=wbZf, [LSL1]=wbZf,
  [ASR]=wbZf, [LSR]=wbZf, [LSLX]=wbZf, [LSL1X]=wbZf, [LSR1]=wbZf, [LSRX]=wbZf,
  [LSR1X]=wbZf, [ROL_ADD]=wbShAdd, [LSR_ADD]=wbShAdd, [LSL_ADD]=wbShAdd,
  [LSL_SUB]=wbShAdd, [NEG]=wbZf, [NOT]=wbZf,

  [MOVE_S]=wbZf_s, [LBS_S]=wbNoZf_s, [LHS_S]=wbNoZf_s, [EXTSB_S]=wbZf_s,
  [EXTSH_S]=wbZf_s, [MUL_SL_SL_S]=wbZf, [MUL_SL_SH_S]=wbZf, [MUL_SH_SL_S]=wbZf,
  [MUL_SH_SH_S]=wbZf, [MUL_SL_UL_S]=wbZf_s, [MUL_SL_UH_S]=wbZf_s,
  [MUL_SH_UL_S]=wbZf_s, [MUL_SH_UH_S]=wbZf_s, [MUL_UL_SL_S]=wbZf_s,
  [MUL_UL_SH_S]=wbZf_s, [MUL_UH_SL_S]=wbZf_s, [MUL_UH_SH_S]=wbZf_s,
  [ADD_S]=wbZf_s, [ADDC_S]=wbZf_s, [SUB_S]=wbZf_s, [SUBC_S]=wbZf_s,
  [AND_S]=wbZf_s, [NAND_S]=wbZf_s, [ANDN_S]=wbZf_s, [OR_S]=wbZf_s,
  [NOR_S]=wbZf_s, [ORN_S]=wbZf_s, [XOR_S]=wbZf_s, [NXOR_S]=wbZf_s,
  [LW_S]=wbNoZf_s, [HASH_S]=wbZf_s, [SATS_S]=wbZf_s, [CMPB4_S]=wbZf_s,
  [ROL_S]=wbZf_s, [ROR_S]=wbZf_s, [LSL_S]=wbZf_s, [LSL1_S]=wbZf_s,
  [ASR_S]=wbZf_s, [LSR_S]=wbZf_s, [LSLX_S]=wbZf_s, [LSL1X_S]=wbZf_s,
  [LSRX_S]=wbZf_s, [LSR1X_S]=wbZf_s, [ROL_ADD_S]=wbShAdd_s,
  [LSR_ADD_S]=wbShAdd_s, [LSL_ADD_S]=wbShAdd_s, [LSL_SUB_S]=wbShAdd_s,

  [MOVE_U]=wbZf_u, [LBU_U]=wbNoZf_u, [LHU_U]=wbNoZf_u, [EXTUB_U]=wbZf_u,
  [EXTUH_U]=wbZf_u, [CLZ_U]=wbZf_u, [CLO_U]=wbZf_u, [CLS_U]=wbZf_u,
  [CAO_U]=wbZf_u, [MUL_UL_UL_U]=wbZf_u, [MUL_UL_UH_U]=wbZf_u,
  [MUL_UH_UL_U]=wbZf_u, [MUL_UH_UH_U]=wbZf_u, [ADD_U]=wbZf_u, [ADDC_U]=wbZf_u,
  [SUB_U]=wbZf_u, [SUBC_U]=wbZf_u, [AND_U]=wbZf_u, [NAND_U]=wbZf_u,
  [ANDN_U]=wbZf_u, [OR_U]=wbZf_u, [NOR_U]=wbZf_u, [ORN_U]=wbZf_u,
  [XOR_U]=wbZf_u, [NXOR_U]=wbZf_u, [LW_U]=wbNoZf_u, [HASH_U]=wbZf_u,
  [SATS_U]=wbZf_u, [CMPB4_U]=wbZf_u, [ROL_U]=wbZf_u, [ROR_U]=wbZf_u,
  [LSL_U]=wbZf_u, [LSL1_U]=wbZf_u, [ASR_U]=wbZf_u, [LSR_U]=wbZf_u,
  [LSLX_U]=wbZf_u, [LSL1X_U]=wbZf_u, [LSRX_U]=wbZf_u, [LSR1X_U]=wbZf_u,
  [ROL_ADD_U]=wbShAdd_u, [LSR_ADD_U]=wbShAdd_u, [LSL_ADD_U]=wbShAdd_u,
  [LSL_SUB_U]=wbShAdd_u,
};

const char* DmmOpStr[NrOpcode] = {
  [LDMA]="ldma", [SDMA]="sdma", [LDMAI]="ldmai", [MUL_STEP]="mul_step",
  [DIV_STEP]="div_step", [MOVD]="movd", [MOVE]="move", [SWAPD]="swapd",
  [LD]="ld", [LW]="lw", [LBS]="lbs", [LBU]="lbu", [LHS]="lhs", [LHU]="lhu",
  [SB]="sb", [SH]="sh", [SW]="sw", [SD]="sd", [SB_ID]="sb_id", [SH_ID]="sh_id",
  [SW_ID]="sw_id", [SD_ID]="sd_id", [EXTSB]="extsb", [EXTSH]="extsh",
  [EXTUB]="extub", [EXTUH]="extuh", [CLZ]="clz", [CLO]="clo", [CLS]="cls",
  [CAO]="cao", [MUL_SL_SL]="mul_sl_sl", [MUL_SL_SH]="mul_sl_sh",
  [MUL_SH_SL]="mul_sh_sl", [MUL_SH_SH]="mul_sh_sh", [MUL_SL_UL]="mul_sl_ul",
  [MUL_SL_UH]="mul_sl_uh", [MUL_SH_UL]="mul_sh_ul", [MUL_SH_UH]="mul_sh_uh",
  [MUL_UL_SL]="mul_ul_sl", [MUL_UL_SH]="mul_ul_sh", [MUL_UH_SL]="mul_uh_sl",
  [MUL_UH_SH]="mul_uh_sh", [MUL_UL_UL]="mul_ul_ul", [MUL_UL_UH]="mul_ul_uh",
  [MUL_UH_UL]="mul_uh_ul", [MUL_UH_UH]="mul_uh_uh", [JMP]="jmp", [CALL]="call",
  [ACQUIRE]="acquire", [RELEASE]="release", [STOP]="stop", [BOOT]="boot",
  [RESUME]="resume", [CLR_RUN]="clr_run", [TIME]="time", [TIME_CFG]="time_cfg",
  [NOP]="nop", [FAULT]="fault", [ADD]="add", [ADDC]="addc", [SUB]="sub",
  [SUBC]="subc", [AND]="and", [NAND]="nand", [ANDN]="andn", [OR]="or",
  [NOR]="nor", [ORN]="orn", [XOR]="xor", [NXOR]="nxor", [HASH]="hash",
  [SATS]="sats", [CMPB4]="cmpb4", [ROL]="rol", [ROR]="ror", [LSL]="lsl",
  [LSL1]="lsl1", [LSLX]="lslx", [LSL1X]="lsl1x", [ASR]="asr", [LSR]="lsr",
  [LSR1]="lsr1", [LSRX]="lsrx", [LSR1X]="lsr1x", [ROL_ADD]="rol_add",
  [LSR_ADD]="lsr_add", [LSL_ADD]="lsl_add", [LSL_SUB]="lsl_sub", [NEG]="neg",
  [NOT]="not",

  // S variant opcodes
  [MOVE_S]="move.s", [LBS_S]="lbs.s", [LHS_S]="lhs.s", [EXTSB_S]="extsb.s",
  [EXTSH_S]="extsh.s", [MUL_SL_SL_S]="mul_sl_sl.s",
  [MUL_SL_SH_S]="mul_sl_sh.s", [MUL_SH_SL_S]="mul_sh_sl.s",
  [MUL_SH_SH_S]="mul_sh_sh.s", [MUL_SL_UL_S]="mul_sl_ul.s",
  [MUL_SL_UH_S]="mul_sl_uh.s", [MUL_SH_UL_S]="mul_sh_ul.s",
  [MUL_SH_UH_S]="mul_sh_uh.s", [MUL_UL_SL_S]="mul_ul_sl.s",
  [MUL_UL_SH_S]="mul_ul_sh.s", [MUL_UH_SL_S]="mul_uh_sl.s",
  [MUL_UH_SH_S]="mul_uh_sh.s", [ADD_S]="add.s", [ADDC_S]="addc.s",
  [SUB_S]="sub.s", [SUBC_S]="subc.s", [AND_S]="and.s", [NAND_S]="nand.s",
  [ANDN_S]="andn.s", [OR_S]="or.s", [NOR_S]="nor.s", [ORN_S]="orn.s",
  [XOR_S]="xor.s", [NXOR_S]="nxor.s", [LW_S]="lw.s", [HASH_S]="hash.s",
  [SATS_S]="sats.s", [CMPB4_S]="cmpb4.s", [ROL_S]="rol.s", [ROR_S]="ror.s",
  [LSL_S]="lsl.s", [LSL1_S]="lsl1.s", [LSLX_S]="lslx.s", [LSL1X_S]="lsl1x.s",
  [ASR_S]="asr.s", [LSR_S]="lsr.s", [LSR1_S]="lsr1.s", [LSRX_S]="lsrx.s",
  [LSR1X_S]="lsr1x.s", [ROL_ADD_S]="rol_add.s", [LSR_ADD_S]="lsr_add.s",
  [LSL_ADD_S]="lsl_add.s", [LSL_SUB_S]="lsl_sub.s",

  // U variant opcodes
  [MOVE_U]="move.u", [LBU_U]="lbu.u", [LHU_U]="lhu.u", [EXTUB_U]="extub.u",
  [EXTUH_U]="extuh.u", [CLZ_U]="clz.u", [CLO_U]="clo.u", [CLS_U]="cls.u",
  [CAO_U]="cao.u", [MUL_UL_UL_U]="mul_ul_ul.u", [MUL_UL_UH_U]="mul_ul_uh.u",
  [MUL_UH_UL_U]="mul_uh_ul.u", [MUL_UH_UH_U]="mul_uh_uh.u", [ADD_U]="add.u",
  [ADDC_U]="addc.u", [SUB_U]="sub.u", [SUBC_U]="subc.u", [AND_U]="and.u",
  [NAND_U]="nand.u", [ANDN_U]="andn.u", [OR_U]="or.u", [NOR_U]="nor.u",
  [ORN_U]="orn.u", [XOR_U]="xor.u", [NXOR_U]="nxor.u", [LW_U]="lw.u",
  [HASH_U]="hash.u", [SATS_U]="sats.u", [CMPB4_U]="cmpb4.u", [ROL_U]="rol.u",
  [ROR_U]="ror.u", [LSL_U]="lsl.u", [LSL1_U]="lsl1.u", [LSLX_U]="lslx.u",
  [LSL1X_U]="lsl1x.u", [ASR_U]="asr.u", [LSR_U]="lsr.u", [LSR1_U]="lsr1.u",
  [LSRX_U]="lsrx.u", [LSR1X_U]="lsr1x.u", [ROL_ADD_U]="rol_add.u",
  [LSR_ADD_U]="lsr_add.u", [LSL_ADD_U]="lsl_add.u", [LSL_SUB_U]="lsl_sub.u",
};

const char *DmmCcStr[NrConds] = {
    "",    "true",  "false", "z",    "nz",   "sz",    "snz",   "pl",
    "mi",  "spl",   "smi",   "v",    "nv",   "c",     "nc",    "ltu",
    "geu", "leu",   "gtu",   "lts",  "ges",  "les",   "gts",   "eq",
    "neq", "xz",    "xnz",   "xleu", "xgtu", "xles",  "xgts",  "se",
    "so",  "nsh32", "sh32",  "max",  "nmax", "small", "large",
};
const char *DmmJccStr[NrConds] = {
    NULL,  NULL,  NULL,  NULL, NULL,  "z",   "nz",  NULL,  NULL,  "spl",
    "smi", NULL,  NULL,  NULL, NULL,  "ltu", "geu", "leu", "gtu", "lts",
    "ges", "les", "gts", "eq", "neq", NULL,  NULL,  NULL,  NULL,  NULL,
    NULL,  NULL,  NULL,  NULL, NULL,  NULL,  NULL,  NULL,  NULL,
};

DmmMap DmmStrToOpcode, DmmStrToCc, DmmStrToJcc;
__attribute__((constructor)) static void init() {
  DmmStrToOpcode = DmmMapInit(512);
  DmmStrToCc = DmmMapInit(128);
  DmmStrToJcc = DmmMapInit(64);
  for (size_t i = 0; i < NrOpcode; ++i)
    DmmMapAssign(DmmStrToOpcode, DmmOpStr[i], strlen(DmmOpStr[i]), i);
  for (size_t i = 1; i < NrConds; ++i) {
    DmmMapAssign(DmmStrToCc, DmmCcStr[i], strlen(DmmCcStr[i]), i);
    if (DmmJccStr[i] != NULL)
      DmmMapAssign(DmmStrToJcc, DmmJccStr[i], strlen(DmmJccStr[i]), i);
  }
}

