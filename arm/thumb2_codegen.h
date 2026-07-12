/* gameplaySP - Thumb-2 (T32) encoder primitives for the Cortex-M7 dynarec
 * backend (Playdate port). Grows alongside arm/thumb2_emit.h; see the
 * Phase 4 workplan in NOTES.md. MOVW/MOVT were validated on device by
 * pd_jit_smoke.c before this backend existed.
 *
 * Conventions: T32 instructions are emitted as one or two halfwords, FIRST
 * halfword first (little-endian within each halfword). `translation_ptr`
 * (u8*) is the emission cursor, as in the other backends.
 */

#ifndef THUMB2_CODEGEN_H
#define THUMB2_CODEGEN_H

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

/* --- Constant loads ------------------------------------------------------ */

/* MOVW (T3) / MOVT (T1): imm16 split as imm4:i:imm3:imm8. */
#define t2_movw(rd, imm16)                                                    \
  t2_write32(0xF240 | ((((imm16) >> 11) & 1) << 10) | (((imm16) >> 12) & 0xF),\
             ((((imm16) >> 8) & 7) << 12) | ((rd) << 8) | ((imm16) & 0xFF))   \

#define t2_movt(rd, imm16)                                                    \
  t2_write32(0xF2C0 | ((((imm16) >> 11) & 1) << 10) | (((imm16) >> 12) & 0xF),\
             ((((imm16) >> 8) & 7) << 12) | ((rd) << 8) | ((imm16) & 0xFF))   \

/* Load an arbitrary 32-bit constant. 8 bytes; a MOVW-only fast path when
 * the top half is zero. */
#define t2_load_imm32(rd, imm32)                                              \
{                                                                             \
  t2_movw(rd, (imm32) & 0xFFFF);                                              \
  if ((u32)(imm32) >> 16)                                                     \
    t2_movt(rd, ((u32)(imm32) >> 16) & 0xFFFF);                               \
}                                                                             \

/* --- Branches ------------------------------------------------------------ */

/* Branch offsets: from = address of the branch instruction's first
 * halfword; T32 reads PC as from+4. Offset must be halfword-aligned.
 * B.W (T4): reach +/-16MB.  cond B.W (T3): reach +/-1MB.  BL (T1): +/-16MB.
 * J1/J2 scrambling: I1 = ~(J1 ^ S), I2 = ~(J2 ^ S). */

#define t2_branch_offset(from, to) ((s32)((u32)(to) - ((u32)(from) + 4)))

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

/* Conditional B.W (T3), cond = ARM condition code 0..13. +/-1MB. */
#define t2_b_cond_w(from, to, cond)                                           \
{                                                                             \
  s32 t2_off = t2_branch_offset(from, to);                                    \
  u32 t2_s = ((u32)t2_off >> 20) & 1;                                         \
  t2_write32(0xF000 | (t2_s << 10) | ((cond) << 6) |                          \
             (((u32)t2_off >> 12) & 0x3F),                                    \
             0x8000 | ((((u32)t2_off >> 18) & 1) << 13) |                     \
             ((((u32)t2_off >> 19) & 1) << 11) | (((u32)t2_off >> 1) & 0x7FF));\
}                                                                             \

/* Register branches (16-bit encodings). */
#define t2_bx(rm)   t2_write16(0x4700 | ((rm) << 3))
#define t2_blx(rm)  t2_write16(0x4780 | ((rm) << 3))

/* --- Conditional execution ----------------------------------------------- */

/* IT (T1): makes the next instruction conditional on `cond`.
 * mask for a single instruction = 0x8. Extend to ITT/ITE later if the
 * emitter wants multi-instruction blocks. */
#define t2_it(cond)      t2_write16(0xBF00 | ((cond) << 4) | 0x8)

/* ARM condition codes (match the ARM backend's numbering). */
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

#endif /* THUMB2_CODEGEN_H */
