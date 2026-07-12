/* Round-trip test: emit instructions with thumb2_codegen.h and compare
 * byte-for-byte against GAS assembling the equivalent source. */
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;

#include "thumb2_codegen.h"

u8 buf[4096];

int main(void)
{
  u8 *translation_ptr = buf;
  u8 *base = buf;

  /* Keep in sync with enc_test.s, one line per emit. */
  t2_dp_imm(T2OP_ADD, 0, 2, 3, t2_imm12(0x4000));      /* add.w r2, r3, #0x4000 */
  t2_dp_imm(T2OP_SUB, 1, 0, 1, t2_imm12(0xFF));        /* subs.w r0, r1, #0xff */
  t2_dp_imm(T2OP_AND, 0, 9, 9, t2_imm12(0xF0000000));  /* and.w r9, r9, #0xf0000000 */
  t2_dp_imm(T2OP_ORR, 0, 4, 4, t2_imm12(0x00AB00AB));  /* orr.w r4, r4, #0x00AB00AB */
  t2_dp_imm(T2OP_BIC, 0, 5, 5, t2_imm12(0xABABABAB));  /* bic.w r5, r5, #0xABABABAB */
  t2_dp_imm(T2OP_SUB, 1, 15, 7, t2_imm12(0));          /* cmp.w r7, #0 */
  t2_dp_imm(T2OP_AND, 1, 15, 8, t2_imm12(0x20));       /* tst.w r8, #0x20 */
  t2_dp_reg(T2OP_ADD, 0, 1, 2, 3, T2SHIFT_LSL, 7);     /* add.w r1, r2, r3, lsl #7 */
  t2_dp_reg(T2OP_SUB, 1, 4, 5, 6, T2SHIFT_ASR, 31);    /* subs.w r4, r5, r6, asr #31 */
  t2_dp_reg(T2OP_ADC, 0, 8, 8, 9, T2SHIFT_ROR, 1);     /* adc.w r8, r8, r9, ror #1 */
  t2_dp_reg(T2OP_RSB, 0, 0, 1, 2, T2SHIFT_LSL, 0);     /* rsb r0, r1, r2 */
  t2_dp_reg(T2OP_ADD, 1, 15, 3, 4, T2SHIFT_LSL, 2);    /* cmn.w r3, r4, lsl #2 */
  t2_mov_reg_shift(0, 2, 12, T2SHIFT_LSR, 31);         /* lsr.w r2, r12, #31 */
  t2_mov_reg_shift(1, 3, 3, T2SHIFT_LSL, 1);           /* lsls.w r3, r3, #1 */
  t2_mvn_reg_shift(0, 6, 7, T2SHIFT_LSL, 0);           /* mvn.w r6, r7 */
  t2_mov16(10, 3);                                     /* mov r10, r3 */
  t2_mov16(2, 11);                                     /* mov r2, r11 */
  t2_shift_reg(1, T2SHIFT_LSL, 4, 5, 6);               /* lsls.w r4, r5, r6 */
  t2_shift_reg(0, T2SHIFT_ROR, 7, 8, 9);               /* ror.w r7, r8, r9 */
  t2_movw(0, 0x1234);                                  /* movw r0, #0x1234 */
  t2_movt(1, 0xABCD);                                  /* movt r1, #0xABCD */
  t2_mul(3, 4, 5);                                     /* mul r3, r4, r5 */
  t2_mla(6, 7, 8, 9);                                  /* mla r6, r7, r8, r9 */
  t2_mls(0, 1, 2, 3);                                  /* mls r0, r1, r2, r3 */
  t2_smull(2, 3, 4, 5);                                /* smull r2, r3, r4, r5 */
  t2_umull(6, 7, 8, 9);                                /* umull r6, r7, r8, r9 */
  t2_smlal(0, 1, 2, 3);                                /* smlal r0, r1, r2, r3 */
  t2_umlal(4, 5, 6, 7);                                /* umlal r4, r5, r6, r7 */
  t2_clz(0, 1);                                        /* clz r0, r1 */
  t2_usat_asr(2, 4, 3, 24);                            /* usat r2, #4, r3, asr #24 */
  t2_ldr_imm(0, 11, 68);                               /* ldr.w r0, [r11, #68] */
  t2_str_imm(9, 11, 64);                               /* str.w r9, [r11, #64] */
  t2_ldr_imm(0, 11, 0x8D0);                            /* ldr.w r0, [r11, #0x8D0] */
  t2_ldr_imm_neg(1, 2, 12);                            /* ldr.w r1, [r2, #-12] */
  t2_str_imm_neg(3, 4, 8);                             /* str.w r3, [r4, #-8] */
  t2_ldr_reg(2, 11, 2, 2);                             /* ldr.w r2, [r11, r2, lsl #2] */
  t2_str_reg(1, 0, 2, 0);                              /* str.w r1, [r0, r2] */
  t2_mrs_apsr(9);                                      /* mrs r9, apsr */
  t2_msr_apsr(9);                                      /* msr apsr_nzcvq, r9 */
  t2_bx(14);                                           /* bx lr */
  t2_blx(2);                                           /* blx r2 */
  t2_it(T2_CC_NE);                                     /* it ne */
  t2_mov16(1, 2);                                      /* movne r1, r2 */
  t2_nop();                                            /* nop */
  t2_push(0x500C);                                     /* push.w {r2,r3,r12,lr} */
  t2_pop(0x500C);                                      /* pop.w {r2,r3,r12,lr} */
  t2_cbnz(0, 8);                                       /* cbnz r0, 99f (5 nops) */
  t2_nop(); t2_nop(); t2_nop(); t2_nop(); t2_nop();
  /* branches: to a label at +64 bytes from each instruction start */
  { u8 *from = translation_ptr; t2_b_w(from, from + 64); }
  { u8 *from = translation_ptr; t2_bl(from, from + 64); }
  { u8 *from = translation_ptr; t2_b_cond_w(from, from + 64, T2_CC_EQ); }
  { u8 *from = translation_ptr; t2_b_cond_w(from, from - 64, T2_CC_LT); }
  { u8 *from = translation_ptr; t2_b_w(from, from - 1024); }

  t2_nop();                                            /* final pad, keeps .text 4-aligned */

  fwrite(buf, 1, translation_ptr - buf, stdout);
  return 0;
}
