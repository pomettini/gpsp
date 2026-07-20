/* gameplaySP - Thumb-2 (T32) encoder primitives for the Cortex-M7 dynarec
 * backend (Playdate port). Used by arm/thumb2_emit.h; see the Phase 4
 * workplan in NOTES.md. MOVW/MOVT were validated on device by
 * pd_jit_smoke.c before this backend existed.
 *
 * Conventions: T32 instructions are one or two halfwords, FIRST halfword
 * first (little-endian within each halfword). `translation_ptr` (u8*) is
 * the emission cursor, as in the other backends. 32-bit (.W) encodings are
 * used throughout so sizes are predictable for patching; the only 16-bit
 * forms are MOV(reg), BX/BLX(reg), IT and NOP, which are never patched.
 * Branch targets are masked with ~1 (function pointers carry the Thumb
 * bit). Encodings follow the ARMv7-M ARM.
 */

#ifndef THUMB2_CODEGEN_H
#define THUMB2_CODEGEN_H

#include <stdint.h>

/* Host registers (numeric). */
#define T2REG_LR  14
#define T2REG_PC  15

/* Shift types (same numbering as ARMSHIFT_*). */
#define T2SHIFT_LSL 0
#define T2SHIFT_LSR 1
#define T2SHIFT_ASR 2
#define T2SHIFT_ROR 3

/* ARM condition codes. */
#define T2_CC_EQ 0x0
#define T2_CC_NE 0x1
#define T2_CC_CS 0x2
#define T2_CC_CC 0x3
#define T2_CC_MI 0x4
#define T2_CC_PL 0x5
#define T2_CC_VS 0x6
#define T2_CC_VC 0x7
#define T2_CC_HI 0x8
#define T2_CC_LS 0x9
#define T2_CC_GE 0xA
#define T2_CC_LT 0xB
#define T2_CC_GT 0xC
#define T2_CC_LE 0xD
#define T2_CC_AL 0xE

/* Data-processing opcodes (shared by shifted-register and modified-imm
 * encodings). MOV/MVN are ORR/ORN with rn=15; TST/TEQ/CMP/CMN are the S
 * forms with rd=15. */
#define T2OP_AND 0x0
#define T2OP_BIC 0x1
#define T2OP_ORR 0x2
#define T2OP_ORN 0x3
#define T2OP_EOR 0x4
#define T2OP_ADD 0x8
#define T2OP_ADC 0xA
#define T2OP_SBC 0xB
#define T2OP_SUB 0xD
#define T2OP_RSB 0xE

#define t2_write16(hw)                                                        \
{                                                                             \
  *((u16 *)translation_ptr) = (u16)(hw);                                      \
  translation_ptr += 2;                                                       \
}                                                                             \

#define t2_write32(hw1, hw2)                                                  \
{                                                                             \
  t2_write16(hw1);                                                            \
  t2_write16(hw2);                                                            \
}                                                                             \

/* --- Modified immediate ---------------------------------------------------
 * Returns the 12-bit i:imm3:imm8 field for a T32 modified immediate, or
 * 0xFFFFFFFF if not encodable. Bit layout of the return: [11:8]=i:imm3,
 * [7:0]=imm8 (split into the instruction by t2_dp_imm below). Every ARM
 * rotated immediate is encodable, so guest constants always fit. */
static inline u32 t2_imm12(u32 v)
{
  u32 rot, u;
  if (v < 256)
    return v;
  if (((v >> 16) & 0xFFFF) == (v & 0xFFFF))
  {
    u32 h = v & 0xFFFF;
    if (!(h & 0xFF00))
      return 0x100 | h;                            /* 0x00XY00XY */
    if (!(h & 0x00FF))
      return 0x200 | (h >> 8);                     /* 0xXY00XY00 */
    if ((h >> 8) == (h & 0xFF))
      return 0x300 | (h & 0xFF);                   /* 0xXYXYXYXY */
  }
  for (rot = 8; rot < 32; rot++)
  {
    u = (v << rot) | (v >> (32 - rot));            /* undo the ROR */
    if (u < 256 && (u & 0x80))
      return (rot << 7) | (u & 0x7F);              /* [11:7]=rot [6:0]=low */
  }
  return 0xFFFFFFFF;
}

/* --- Data processing, modified immediate ----------------------------------
 * imm12 comes from t2_imm12 (must be valid). S in {0,1}. */
static inline void t2_dp_imm_f(u8 **tp, u32 op, u32 s, u32 rd, u32 rn,
                               u32 imm12)
{
  u8 *translation_ptr = *tp;
#ifdef PD_NARROW
  if (s && imm12 < 256)     /* plain imm8, no rotation */
  {
    u32 done = 1;
    if (op == T2OP_SUB && rd == 15 && rn < 8)
      t2_write16(0x2800 | (rn << 8) | imm12)         /* CMP  */
    else if (op == T2OP_ORR && rn == 15 && rd < 8)
      t2_write16(0x2000 | (rd << 8) | imm12)         /* MOVS */
    else if (op == T2OP_ADD && rd < 8 && rd == rn)
      t2_write16(0x3000 | (rd << 8) | imm12)         /* ADDS rdn, #imm8 */
    else if (op == T2OP_SUB && rd < 8 && rd == rn)
      t2_write16(0x3800 | (rd << 8) | imm12)         /* SUBS rdn, #imm8 */
    else if (op == T2OP_RSB && imm12 == 0 && rd < 8 && rn < 8)
      t2_write16(0x4240 | (rn << 3) | rd)            /* NEGS */
    else
      done = 0;
    if (done) { *tp = translation_ptr; return; }
  }
#endif
  t2_write32(0xF000 | (((imm12 >> 11) & 1) << 10) | (op << 5) |
             (s << 4) | rn,
             (((imm12 >> 8) & 7) << 12) | (rd << 8) | (imm12 & 0xFF));
  *tp = translation_ptr;
}
#define t2_dp_imm(op, s, rd, rn, imm12)                                       \
  t2_dp_imm_f(&translation_ptr, op, s, rd, rn, imm12)                         \

/* ADDW/SUBW: plain 12-bit immediate (0..4095), no flags. For addresses. */
#define t2_addw(rd, rn, imm)                                                  \
  t2_write32(0xF200 | ((((imm) >> 11) & 1) << 10) | (rn),                     \
             ((((imm) >> 8) & 7) << 12) | ((rd) << 8) | ((imm) & 0xFF))       \

#define t2_subw(rd, rn, imm)                                                  \
  t2_write32(0xF2A0 | ((((imm) >> 11) & 1) << 10) | (rn),                     \
             ((((imm) >> 8) & 7) << 12) | ((rd) << 8) | ((imm) & 0xFF))       \

/* --- Data processing, shifted register ------------------------------------
 * shift = 0..31, type = T2SHIFT_*. Narrow (16-bit T1) forms are selected
 * when exactly equivalent: S set (T1 ALU always sets flags), low regs,
 * encodable shape. GAS makes the same choices, so the roundtrip test
 * remains the oracle. */
static inline void t2_dp_reg_f(u8 **tp, u32 op, u32 s, u32 rd, u32 rn,
                               u32 rm, u32 type, u32 shift)
{
  u8 *translation_ptr = *tp;
#ifdef PD_NARROW
  if (s && shift == 0 && type == T2SHIFT_LSL && rn < 8 && rm < 8)
  {
    u32 done = 1;
    if (rd == 15)          /* TST/CMP/CMN (rd=PC S-forms) */
    {
      if (op == T2OP_AND)      t2_write16(0x4200 | (rm << 3) | rn)
      else if (op == T2OP_SUB) t2_write16(0x4280 | (rm << 3) | rn)
      else if (op == T2OP_ADD) t2_write16(0x42C0 | (rm << 3) | rn)
      else done = 0;
    }
    else if (rd < 8)
    {
      if (op == T2OP_ADD)      t2_write16(0x1800 | (rm << 6) | (rn << 3) | rd)
      else if (op == T2OP_SUB) t2_write16(0x1A00 | (rm << 6) | (rn << 3) | rd)
      else if (rd == rn)
      {
        if (op == T2OP_AND)      t2_write16(0x4000 | (rm << 3) | rd)
        else if (op == T2OP_EOR) t2_write16(0x4040 | (rm << 3) | rd)
        else if (op == T2OP_ADC) t2_write16(0x4140 | (rm << 3) | rd)
        else if (op == T2OP_SBC) t2_write16(0x4180 | (rm << 3) | rd)
        else if (op == T2OP_ORR) t2_write16(0x4300 | (rm << 3) | rd)
        else if (op == T2OP_BIC) t2_write16(0x4380 | (rm << 3) | rd)
        else done = 0;
      }
      else done = 0;
    }
    else done = 0;
    if (done) { *tp = translation_ptr; return; }
  }
  /* MOVS/MVNS with rn=15 */
  if (s && rn == 15 && rd < 8 && rm < 8)
  {
    if (op == T2OP_ORR && type != T2SHIFT_ROR)
    {                       /* MOVS = LSLS #0; LSLS/LSRS/ASRS #imm5 */
      t2_write16((type << 11) | ((shift & 31) << 6) | (rm << 3) | rd);
      *tp = translation_ptr; return;
    }
    if (op == T2OP_ORN && shift == 0 && type == T2SHIFT_LSL)
    {
      t2_write16(0x43C0 | (rm << 3) | rd);   /* MVNS rd, rm */
      *tp = translation_ptr; return;
    }
  }
#endif
  t2_write32(0xEA00 | (op << 5) | (s << 4) | rn,
             (((shift >> 2) & 7) << 12) | (rd << 8) |
             ((shift & 3) << 6) | (type << 4) | rm);
  *tp = translation_ptr;
}
#define t2_dp_reg(op, s, rd, rn, rm, type, shift)                             \
  t2_dp_reg_f(&translation_ptr, op, s, rd, rn, rm, type, shift)               \

/* MOV/MVN (shifted register): rn=15. */
#define t2_mov_reg_shift(s, rd, rm, type, shift)                              \
  t2_dp_reg(T2OP_ORR, s, rd, 15, rm, type, shift)                             \

#define t2_mvn_reg_shift(s, rd, rm, type, shift)                              \
  t2_dp_reg(T2OP_ORN, s, rd, 15, rm, type, shift)                             \

/* 16-bit MOV (register), no flags, any regs. */
#define t2_mov16(rd, rm)                                                      \
  t2_write16(0x4600 | (((rd) & 8) << 4) | ((rm) << 3) | ((rd) & 7))           \

/* --- Shift by register (LSL/LSR/ASR/ROR) ---------------------------------- */
#define t2_shift_reg(s, type, rd, rn, rm)                                     \
  t2_write32(0xFA00 | ((type) << 5) | ((s) << 4) | (rn),                      \
             0xF000 | ((rd) << 8) | (rm))                                     \

/* --- Constant loads -------------------------------------------------------- */
#define t2_movw(rd, imm16)                                                    \
  t2_write32(0xF240 | ((((imm16) >> 11) & 1) << 10) | (((imm16) >> 12) & 0xF),\
             ((((imm16) >> 8) & 7) << 12) | ((rd) << 8) | ((imm16) & 0xFF))   \

#define t2_movt(rd, imm16)                                                    \
  t2_write32(0xF2C0 | ((((imm16) >> 11) & 1) << 10) | (((imm16) >> 12) & 0xF),\
             ((((imm16) >> 8) & 7) << 12) | ((rd) << 8) | ((imm16) & 0xFF))   \

#define t2_load_imm32_mov(rd, imm32)                                          \
{                                                                             \
  t2_movw(rd, (imm32) & 0xFFFF);                                              \
  if ((u32)(imm32) >> 16)                                                     \
  {                                                                           \
    t2_movt(rd, ((u32)(imm32) >> 16) & 0xFFFF);                               \
  }                                                                           \
}                                                                             \

#ifndef PD_LIT_POOL
#define t2_load_imm32 t2_load_imm32_mov
#endif

/* --- Multiplies ------------------------------------------------------------ */
#define t2_mul(rd, rn, rm)                                                    \
  t2_write32(0xFB00 | (rn), 0xF000 | ((rd) << 8) | (rm))                      \

#define t2_mla(rd, rn, rm, ra)                                                \
  t2_write32(0xFB00 | (rn), ((ra) << 12) | ((rd) << 8) | (rm))                \

#define t2_mls(rd, rn, rm, ra)                                                \
  t2_write32(0xFB00 | (rn), ((ra) << 12) | ((rd) << 8) | 0x10 | (rm))         \

#define t2_smull(rdlo, rdhi, rn, rm)                                          \
  t2_write32(0xFB80 | (rn), ((rdlo) << 12) | ((rdhi) << 8) | (rm))            \

#define t2_umull(rdlo, rdhi, rn, rm)                                          \
  t2_write32(0xFBA0 | (rn), ((rdlo) << 12) | ((rdhi) << 8) | (rm))            \

#define t2_smlal(rdlo, rdhi, rn, rm)                                          \
  t2_write32(0xFBC0 | (rn), ((rdlo) << 12) | ((rdhi) << 8) | (rm))            \

#define t2_umlal(rdlo, rdhi, rn, rm)                                          \
  t2_write32(0xFBE0 | (rn), ((rdlo) << 12) | ((rdhi) << 8) | (rm))            \

#define t2_clz(rd, rm)                                                        \
  t2_write32(0xFAB0 | (rm), 0xF080 | ((rd) << 8) | (rm))                      \

/* USAT rd, #sat, rn, ASR #shift (shift 1..31). */
#define t2_usat_asr(rd, sat, rn, shift)                                       \
  t2_write32(0xF3A0 | (rn),                                                   \
             ((((shift) >> 2) & 7) << 12) | ((rd) << 8) |                     \
             (((shift) & 3) << 6) | (sat))                                    \

/* --- Loads/stores ----------------------------------------------------------
 * imm12 form: 0 <= imm <= 4095. Register form: shift = LSL 0..3. */
#define t2_ldr_imm(rt, rn, imm)                                               \
  t2_write32(0xF8D0 | (rn), ((rt) << 12) | ((imm) & 0xFFF))                   \

#define t2_str_imm(rt, rn, imm)                                               \
  t2_write32(0xF8C0 | (rn), ((rt) << 12) | ((imm) & 0xFFF))                   \

/* Negative offset forms (imm8, P=1 U=0 W=0). 0 <= imm <= 255. */
#define t2_ldr_imm_neg(rt, rn, imm)                                           \
  t2_write32(0xF850 | (rn), ((rt) << 12) | 0x0C00 | ((imm) & 0xFF))           \

#define t2_str_imm_neg(rt, rn, imm)                                           \
  t2_write32(0xF840 | (rn), ((rt) << 12) | 0x0C00 | ((imm) & 0xFF))           \

#define t2_ldr_reg(rt, rn, rm, lsl)                                           \
  t2_write32(0xF850 | (rn), ((rt) << 12) | ((lsl) << 4) | (rm))               \

#define t2_str_reg(rt, rn, rm, lsl)                                           \
  t2_write32(0xF840 | (rn), ((rt) << 12) | ((lsl) << 4) | (rm))               \

/* --- Flags ------------------------------------------------------------------ */
#define t2_mrs_apsr(rd)                                                       \
  t2_write32(0xF3EF, 0x8000 | ((rd) << 8))                                    \

#define t2_msr_apsr(rn)                                                       \
  t2_write32(0xF380 | (rn), 0x8800)                                           \

/* --- Branches ----------------------------------------------------------------
 * from = address of the branch's first halfword; T32 reads PC as from+4.
 * Targets are masked with ~1 (Thumb-bit tolerant). J1/J2: I1=~(J1^S),
 * I2=~(J2^S). */
#define t2_branch_offset(from, to)                                            \
  ((s32)(((u32)(uintptr_t)(to) & ~1U) - ((u32)(uintptr_t)(from) + 4)))                              \

#define t2_b_w(from, to)                                                      \
{                                                                             \
  s32 t2_off = t2_branch_offset(from, to);                                    \
  u32 t2_s = ((u32)t2_off >> 24) & 1;                                         \
  u32 t2_i1 = ((u32)t2_off >> 23) & 1, t2_i2 = ((u32)t2_off >> 22) & 1;       \
  t2_write32(0xF000 | (t2_s << 10) | (((u32)t2_off >> 12) & 0x3FF),           \
             0x9000 | ((~(t2_i1 ^ t2_s) & 1) << 13) |                         \
             ((~(t2_i2 ^ t2_s) & 1) << 11) | (((u32)t2_off >> 1) & 0x7FF));   \
}                                                                             \

#define t2_bl(from, to)                                                       \
{                                                                             \
  s32 t2_off = t2_branch_offset(from, to);                                    \
  u32 t2_s = ((u32)t2_off >> 24) & 1;                                         \
  u32 t2_i1 = ((u32)t2_off >> 23) & 1, t2_i2 = ((u32)t2_off >> 22) & 1;       \
  t2_write32(0xF000 | (t2_s << 10) | (((u32)t2_off >> 12) & 0x3FF),           \
             0xD000 | ((~(t2_i1 ^ t2_s) & 1) << 13) |                         \
             ((~(t2_i2 ^ t2_s) & 1) << 11) | (((u32)t2_off >> 1) & 0x7FF));   \
}                                                                             \

/* Conditional B.W (T3), reach +/-1MB. cond must not be AL/NV. */
#define t2_b_cond_w(from, to, cond)                                           \
{                                                                             \
  s32 t2_off = t2_branch_offset(from, to);                                    \
  u32 t2_s = ((u32)t2_off >> 20) & 1;                                         \
  t2_write32(0xF000 | (t2_s << 10) | ((cond) << 6) |                          \
             (((u32)t2_off >> 12) & 0x3F),                                    \
             0x8000 | ((((u32)t2_off >> 18) & 1) << 13) |                     \
             ((((u32)t2_off >> 19) & 1) << 11) |                              \
             (((u32)t2_off >> 1) & 0x7FF));                                   \
}                                                                             \

/* Patch a previously-emitted branch at dest (from generate_branch_filler:
 * T3 conditional or T4 unconditional, distinguished by hw2 bit 12). */
static inline void t2_patch_branch(u8 *dest, u8 *target)
{
  u16 *hw = (u16 *)dest;
  s32 off = (s32)(((u32)(uintptr_t)target & ~1U) -
                  ((u32)(uintptr_t)dest + 4));
  if (hw[1] & 0x1000)
  {
    /* T4 unconditional */
    u32 s = ((u32)off >> 24) & 1;
    u32 i1 = ((u32)off >> 23) & 1, i2 = ((u32)off >> 22) & 1;
    hw[0] = (hw[0] & 0xF800) | (s << 10) | (((u32)off >> 12) & 0x3FF);
    hw[1] = 0x9000 | ((~(i1 ^ s) & 1) << 13) | ((~(i2 ^ s) & 1) << 11) |
            (((u32)off >> 1) & 0x7FF);
  }
  else
  {
    /* T3 conditional (keep cond field) */
    u32 s = ((u32)off >> 20) & 1;
    hw[0] = (hw[0] & 0xFBC0) | (s << 10) | (((u32)off >> 12) & 0x3F);
    hw[1] = 0x8000 | ((((u32)off >> 18) & 1) << 13) |
            ((((u32)off >> 19) & 1) << 11) | (((u32)off >> 1) & 0x7FF);
  }
}

/* Register branches (16-bit). */
#define t2_bx(rm)   t2_write16(0x4700 | ((rm) << 3))
#define t2_blx(rm)  t2_write16(0x4780 | ((rm) << 3))

/* CBZ/CBNZ rn, +imm (16-bit, rn < 8, forward 0..126 bytes). */
#define t2_cbnz(rn, imm)                                                      \
  t2_write16(0xB900 | ((((imm) >> 6) & 1) << 9) | ((((imm) >> 1) & 0x1F) << 3)\
             | (rn))                                                          \

#define t2_cbz(rn, imm)                                                       \
  t2_write16(0xB100 | ((((imm) >> 6) & 1) << 9) | ((((imm) >> 1) & 0x1F) << 3)\
             | (rn))                                                          \

/* UBFX rd, rn, #lsb, #width. */
#define t2_ubfx(rd, rn, lsb, width)                                           \
  t2_write32(0xF3C0 | (rn),                                                   \
             ((((lsb) >> 2) & 7) << 12) | ((rd) << 8) |                       \
             (((lsb) & 3) << 6) | ((width) - 1))                              \

/* Byte/halfword loads, register offset (LSL 0..3). */
#define t2_ldrb_reg(rt, rn, rm, lsl)                                          \
  t2_write32(0xF810 | (rn), ((rt) << 12) | ((lsl) << 4) | (rm))               \

#define t2_ldrsb_reg(rt, rn, rm, lsl)                                         \
  t2_write32(0xF910 | (rn), ((rt) << 12) | ((lsl) << 4) | (rm))               \

#define t2_ldrh_reg(rt, rn, rm, lsl)                                          \
  t2_write32(0xF830 | (rn), ((rt) << 12) | ((lsl) << 4) | (rm))               \

#define t2_ldrsh_reg(rt, rn, rm, lsl)                                         \
  t2_write32(0xF930 | (rn), ((rt) << 12) | ((lsl) << 4) | (rm))               \

/* IT: next instruction conditional on cond. */
#define t2_it(cond)      t2_write16(0xBF00 | ((cond) << 4) | 0x8)

#define t2_nop()         t2_write16(0xBF00)

/* PUSH.W/POP.W with a 16-bit register list mask. */
#define t2_push(list)    t2_write32(0xE92D, (list))
#define t2_pop(list)     t2_write32(0xE8BD, (list))


/* --- PC-literal pools (PD_LIT_POOL) ----------------------------------------
 * 32-bit constants (mostly guest PCs) load with a 4-byte LDR (literal)
 * against a per-block pool instead of an 8-byte MOVW/MOVT pair, shrinking
 * the EXECUTED stream (the pool itself is never fetched as code). Sites
 * are recorded with a zero offset and backpatched when the pool is
 * emitted: normally once per block after the translation gate, or
 * mid-block (branching over the pool) when the translate loop's hook
 * sees the pending span nearing the LDR-literal +4095 reach (2560-byte
 * guard: worst remaining growth is one guest instruction plus the pool
 * itself, bounded well under the 1535 bytes of slack). Constants
 * below 64K keep a single MOVW (same size, no pool entry). */
#ifdef PD_LIT_POOL

#define T2_LIT_MAX 128

static u32 t2_lit_vals[T2_LIT_MAX];
static u8 *t2_lit_sites[T2_LIT_MAX];
static u8  t2_lit_vidx[T2_LIT_MAX];
static u32 t2_lit_nvals, t2_lit_nsites;

static inline void t2_lit_reset(void)
{
  t2_lit_nvals = 0;
  t2_lit_nsites = 0;
}

/* Record a pooled load site and emit the LDR with offset 0 (patched at
 * flush). Returns 0 when tables are full; caller falls back to MOVW/MOVT. */
static inline int t2_lit_load(u8 **tp, u32 rd, u32 val)
{
  u32 i;
  u16 *hw;
  if (t2_lit_nsites >= T2_LIT_MAX)
    return 0;
  for (i = 0; i < t2_lit_nvals; i++)
    if (t2_lit_vals[i] == val)
      break;
  if (i == t2_lit_nvals)
  {
    if (t2_lit_nvals >= T2_LIT_MAX)
      return 0;
    t2_lit_vals[t2_lit_nvals++] = val;
  }
  t2_lit_sites[t2_lit_nsites] = *tp;
  t2_lit_vidx[t2_lit_nsites++] = (u8)i;
  hw = (u16 *)*tp;
  hw[0] = 0xF8DF;                        /* LDR.W rd, [pc, #off] (U=1) */
  hw[1] = (u16)(rd << 12);
  *tp += 4;
  return 1;
}

/* Emit the pool at *tp and backpatch every pending site. The caller
 * guarantees no execution path reaches *tp (end of block, or after a
 * branch emitted by t2_lit_flush_mid). */
static inline void t2_lit_flush(u8 **tp)
{
  u32 i;
  u8 *pool;
  if (!t2_lit_nsites)
  {
    t2_lit_nvals = 0;
    return;
  }
  if ((uintptr_t)*tp & 2)
  {
    *(u16 *)*tp = 0xBF00;                /* alignment pad, never executed */
    *tp += 2;
  }
  pool = *tp;
  for (i = 0; i < t2_lit_nvals; i++)
  {
    *(u32 *)*tp = t2_lit_vals[i];
    *tp += 4;
  }
  for (i = 0; i < t2_lit_nsites; i++)
  {
    u8 *site = t2_lit_sites[i];
    u32 base = ((u32)(uintptr_t)site + 4) & ~3U;
    u32 off = (u32)((uintptr_t)pool + 4 * t2_lit_vidx[i]) - base;
    ((u16 *)site)[1] |= (u16)off;        /* off <= 4095 by the span guard */
  }
  t2_lit_reset();
}

static inline void t2_lit_flush_mid(u8 **tp)
{
  u8 *bsite;
  if (!t2_lit_nsites)
    return;
  bsite = *tp;
  {
    u8 *translation_ptr = *tp;
    t2_b_w(translation_ptr, translation_ptr);  /* placeholder, patched */
    *tp = translation_ptr;
  }
  t2_lit_flush(tp);
  t2_patch_branch(bsite, *tp);
}

/* Flush before the pending span outgrows LDR-literal reach: the oldest
 * site must stay within +4095 of the pool, minus the pool's own worst
 * case (128*4 + pad) and the emitter's per-instruction growth. */
#define t2_lit_need_flush()                                                   \
  (t2_lit_nsites &&                                                           \
   ((t2_lit_nsites >= 96) ||                                                  \
    ((u32)(translation_ptr - t2_lit_sites[0]) > 2560)))

#define t2_load_imm32(rd, imm32)                                              \
{                                                                             \
  if (!((u32)(imm32) >> 16) ||                                                \
      !t2_lit_load(&translation_ptr, (rd), (u32)(imm32)))                     \
    t2_load_imm32_mov(rd, imm32);                                             \
}                                                                             \

#endif /* PD_LIT_POOL */

#endif /* THUMB2_CODEGEN_H */
