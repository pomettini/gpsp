/* gameplaySP - Thumb-2 (Cortex-M7) dynarec backend, ported from arm_emit.h
 * for the Playdate. Register conventions match the ARM backend (r3-r8
 * allocatable, r9 flags/scratch, r11 reg_base, r12 cycles); encodings come
 * from thumb2_codegen.h (GAS-roundtrip tested, see tests/thumb2gen.c).
 *
 * Deviations from the ARM backend (NOTES.md "M-profile deltas"):
 *  - No inline literals after calls: the guest PC rides in r10 (reg_gpc),
 *    loaded with MOVW/MOVT before any handler call that needs it; stubs
 *    return with plain `bx lr`. r10 doubles as emitter scratch between
 *    calls. execute_store_cpsr takes masks in r1 (user) / r2 (privileged).
 *  - No register-shifted-register ALU ops in T32: two-instruction
 *    sequences via r10; the shift sets C (lsls) for logical S-forms, the
 *    following register ALU op leaves C intact, matching ARM semantics.
 *  - No RSC: rsc rd,rn,op2 == adc(rd, ~rn, op2), emitted as MVN+ADC.
 *  - No S-suffix multiplies: MUL/MLA + "MOVS rd, rd" for N/Z; long
 *    multiplies use "ORRS r10, lo, hi" (Z exact; N approximated by
 *    lo31|hi31 - games do not rely on MULLS N in practice).
 *  - SWI 6/7 division uses the hardware SDIV via a stub helper instead of
 *    the emitted bit-by-bit divider.
 */

#ifndef THUMB2_EMIT_H
#define THUMB2_EMIT_H

#include "thumb2_codegen.h"

u32 arm_prepare_load_reg(u8 **tptr, u32 scratch_reg, u32 reg_index);
u32 arm_prepare_load_reg_pc(u8 **tptr, u32 scratch_reg, u32 reg_index, u32 pc_offset);
u32 arm_prepare_store_reg(u32 scratch_reg, u32 reg_index);
u32 thumb_prepare_load_reg(u8 **tptr, u32 scratch_reg, u32 reg_index);
u32 thumb_prepare_load_reg_pc(u8 **tptr, u32 scratch_reg, u32 reg_index, u32 pc_offset);
u32 thumb_prepare_store_reg(u32 scratch_reg, u32 reg_index);

void thumb_cheat_hook(void);
void arm_cheat_hook(void);

u32 arm_update_gba_arm(u32 pc);
u32 arm_update_gba_thumb(u32 pc);
u32 arm_update_gba_idle_arm(u32 pc);
u32 arm_update_gba_idle_thumb(u32 pc);

/* Although these are defined as a function, don't call them as
 * such (jump to it instead) */
void arm_indirect_branch_arm(u32 address);
void arm_indirect_branch_thumb(u32 address);
void arm_indirect_branch_dual_arm(u32 address);
void arm_indirect_branch_dual_thumb(u32 address);

void execute_store_cpsr(u32 new_cpsr);
u32 execute_store_cpsr_body(u32 _cpsr, u32 store_mask, u32 address);
u32 execute_spsr_restore(u32 address);

void execute_swi_arm(u32 pc);
void execute_swi_thumb(u32 pc);

/* Hardware-SDIV division helpers, in thumb2_stub.S */
void t2_hle_div(void);
void t2_hle_divarm(void);
#ifdef PD_M4A_HLE
void t2_hle_m4a_inner(void);
void t2_hle_m4a_mixdown(void);
#endif
#ifdef PD_FIRERED_SPRITE_HLE
void t2_hle_firered_thumb(void);
#endif

#define armfn_gbaup_idle_arm       0
#define armfn_gbaup_idle_thumb     1
#define armfn_gbaup_arm            2
#define armfn_gbaup_thumb          3
#define armfn_swi_arm              4
#define armfn_swi_thumb            5
#define armfn_cheat_arm            6
#define armfn_cheat_thumb          7
#define armfn_store_cpsr           8
#define armfn_spsr_restore         9
#define armfn_indirect_arm        10
#define armfn_indirect_thumb      11
#define armfn_indirect_dual_arm   12
#define armfn_indirect_dual_thumb 13
#define armfn_debug_trace         14

#define STORE_TBL_OFF     0x118
#define SPSR_RAM_OFF      0x100

#define reg_a0          0
#define reg_a1          1
#define reg_a2          2

/* scratch0 is r9 (reg_flags is only live inside the stubs) */
#define reg_s0          9
#define reg_base        11
#define reg_flags       9
/* r10: guest-PC carrier for handler calls, free scratch in between */
#define reg_gpc         10

#define reg_cycles      12

#define reg_rv          0

#define reg_rm          0
#define reg_rn          1
#define reg_rs          14
#define reg_rd          0

#define reg_x0          3
#define reg_x1          4
#define reg_x2          5
#define reg_x3          6
#define reg_x4          7
#define reg_x5          8

#define mem_reg        (~0U)

/* Same allocation as the ARM backend (see its statistics). */
u32 arm_register_allocation[] =
{
  reg_x0,       /* GBA r0  */
  reg_x1,       /* GBA r1  */
  mem_reg,      /* GBA r2  */
  mem_reg,      /* GBA r3  */
  mem_reg,      /* GBA r4  */
  mem_reg,      /* GBA r5  */
  reg_x2,       /* GBA r6  */
  mem_reg,      /* GBA r7  */
  mem_reg,      /* GBA r8  */
  reg_x3,       /* GBA r9  */
  mem_reg,      /* GBA r10 */
  mem_reg,      /* GBA r11 */
  reg_x4,       /* GBA r12 */
  mem_reg,      /* GBA r13 */
  reg_x5,       /* GBA r14 */
  reg_a0,       /* GBA r15 */
  mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg,
  mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg,
};

u32 thumb_register_allocation[] =
{
  reg_x0,       /* GBA r0  */
  reg_x1,       /* GBA r1  */
  reg_x2,       /* GBA r2  */
  reg_x3,       /* GBA r3  */
  reg_x4,       /* GBA r4  */
  reg_x5,       /* GBA r5  */
  mem_reg,      /* GBA r6  */
  mem_reg,      /* GBA r7  */
  mem_reg,      /* GBA r8  */
  mem_reg,      /* GBA r9  */
  mem_reg,      /* GBA r10 */
  mem_reg,      /* GBA r11 */
  mem_reg,      /* GBA r12 */
  mem_reg,      /* GBA r13 */
  mem_reg,      /* GBA r14 */
  reg_a0,       /* GBA r15 */
  mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg,
  mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg, mem_reg,
};

/* ARM (imm8, ror) pair -> plain 32-bit value. */
#define arm_imm_ror_to_value(imm, ror)                                        \
  (((u32)(imm) >> (ror)) | ((u32)(imm) << (32 - (ror)) ))                     \

#define t2_rot_value(imm, ror)                                                \
  (((ror) == 0) ? (u32)(imm) : arm_imm_ror_to_value((imm), (ror)))            \

/* Emit "op rd, rn, #value" with automatic fallback through r10 when the
 * constant has no T32 modified-immediate encoding. */
#define t2_dp_imm_auto(op, s, rd, rn, value)                                  \
{                                                                             \
  u32 _t2enc = t2_imm12(value);                                               \
  if (_t2enc != 0xFFFFFFFF)                                                   \
  {                                                                           \
    t2_dp_imm(op, s, rd, rn, _t2enc);                                         \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    t2_load_imm32(reg_gpc, value);                                            \
    t2_dp_reg(op, s, rd, rn, reg_gpc, T2SHIFT_LSL, 0);                        \
  }                                                                           \
}                                                                             \

#define generate_load_pc(ireg, new_pc)                                        \
  t2_load_imm32(ireg, (u32)(new_pc))                                          \

#define generate_load_imm(ireg, imm, imm_ror)                                 \
{                                                                             \
  u32 _t2v = t2_rot_value(imm, imm_ror);                                      \
  u32 _t2e = t2_imm12(_t2v);                                                  \
  if (_t2e != 0xFFFFFFFF)                                                     \
  {                                                                           \
    t2_dp_imm(T2OP_ORR, 0, ireg, 15, _t2e);   /* mov ireg, #v */              \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    t2_load_imm32(ireg, _t2v);                                                \
  }                                                                           \
}                                                                             \

#define generate_shift_left(ireg, imm)                                        \
  t2_mov_reg_shift(0, ireg, ireg, T2SHIFT_LSL, imm)                           \

#define generate_shift_right(ireg, imm)                                       \
  t2_mov_reg_shift(0, ireg, ireg, T2SHIFT_LSR, imm)                           \

#define generate_shift_right_arithmetic(ireg, imm)                            \
  t2_mov_reg_shift(0, ireg, ireg, T2SHIFT_ASR, imm)                           \

#define generate_rotate_right(ireg, imm)                                      \
  t2_mov_reg_shift(0, ireg, ireg, T2SHIFT_ROR, imm)                           \

#define generate_add(ireg_dest, ireg_src)                                     \
  t2_dp_reg(T2OP_ADD, 0, ireg_dest, ireg_dest, ireg_src, T2SHIFT_LSL, 0)      \

#define generate_sub(ireg_dest, ireg_src)                                     \
  t2_dp_reg(T2OP_SUB, 0, ireg_dest, ireg_dest, ireg_src, T2SHIFT_LSL, 0)      \

#define generate_or(ireg_dest, ireg_src)                                      \
  t2_dp_reg(T2OP_ORR, 0, ireg_dest, ireg_dest, ireg_src, T2SHIFT_LSL, 0)      \

#define generate_xor(ireg_dest, ireg_src)                                     \
  t2_dp_reg(T2OP_EOR, 0, ireg_dest, ireg_dest, ireg_src, T2SHIFT_LSL, 0)      \

#define generate_add_imm(ireg, imm, imm_ror)                                  \
  t2_dp_imm_auto(T2OP_ADD, 0, ireg, ireg, t2_rot_value(imm, imm_ror))         \

#define generate_sub_imm(ireg, imm, imm_ror)                                  \
  t2_dp_imm_auto(T2OP_SUB, 0, ireg, ireg, t2_rot_value(imm, imm_ror))         \

#define generate_xor_imm(ireg, imm, imm_ror)                                  \
  t2_dp_imm_auto(T2OP_EOR, 0, ireg, ireg, t2_rot_value(imm, imm_ror))         \

#define generate_add_reg_reg_imm(ireg_dest, ireg_src, imm, imm_ror)           \
{                                                                             \
  u32 _t2av = t2_rot_value(imm, imm_ror);                                     \
  if (_t2av < 4096)                                                           \
  {                                                                           \
    t2_addw(ireg_dest, ireg_src, _t2av);                                      \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    t2_dp_imm_auto(T2OP_ADD, 0, ireg_dest, ireg_src, _t2av);                  \
  }                                                                           \
}                                                                             \

#define generate_and_imm(ireg, imm, imm_ror)                                  \
  t2_dp_imm_auto(T2OP_AND, 0, ireg, ireg, t2_rot_value(imm, imm_ror))         \

#define generate_mov(ireg_dest, ireg_src)                                     \
  if(ireg_dest != ireg_src)                                                   \
  {                                                                           \
    t2_mov16(ireg_dest, ireg_src);                                            \
  }                                                                           \

/* Calls functions present in the rom/ram cache or the stub (near) */
#define generate_function_call(function_location)                             \
  t2_bl(translation_ptr, (u32)(uintptr_t)(function_location))                 \

/* Calls functions that might be far, via the function table at reg_base */
#define generate_function_far_call(function_number)                           \
  generate_load_memreg(T2REG_LR, function_number + (u32)REG_USERDEF);         \
  t2_blx(T2REG_LR)                                                            \

/* The branch target is to be filled in later (thus a self-branch for now) */
#define generate_branch_filler(condition_code, writeback_location)            \
  (writeback_location) = translation_ptr;                                     \
  if((condition_code) == 0x0E)                                                \
  {                                                                           \
    t2_b_w(translation_ptr, translation_ptr);                                 \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    t2_b_cond_w(translation_ptr, translation_ptr, condition_code);            \
  }                                                                           \

#define generate_update_pc(new_pc)                                            \
  generate_load_pc(reg_a0, new_pc)                                            \

#define generate_cycle_update()                                               \
  if(cycle_count)                                                             \
  {                                                                           \
    if(cycle_count < 4096)                                                    \
    {                                                                         \
      t2_addw(reg_cycles, reg_cycles, cycle_count);  /* one ADDW covers it */ \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      t2_dp_imm_auto(T2OP_ADD, 0, reg_cycles, reg_cycles,                     \
                     cycle_count & ~0xFFU);                                   \
      t2_dp_imm_auto(T2OP_ADD, 0, reg_cycles, reg_cycles,                     \
                     cycle_count & 0xFF);                                     \
    }                                                                         \
    cycle_count = 0;                                                          \
  }                                                                           \

#define generate_branch_patch_conditional(dest, offset)                       \
  t2_patch_branch((u8 *)(dest), (u8 *)(offset))                               \

#define generate_branch_patch_unconditional(dest, offset)                     \
  t2_patch_branch((u8 *)(dest), (u8 *)(offset))                               \

/* The idle version could be optimized to put the CPU into halt mode. */

#define generate_branch_idle_eliminate(writeback_location, new_pc, mode)      \
  t2_load_imm32(reg_gpc, new_pc);                                             \
  generate_function_far_call(armfn_gbaup_idle_##mode);                        \
  generate_branch_filler(0x0E, writeback_location)                            \

/* If the cycle counter is still negative, skip the update_gba call:
 * CBNZ over the 6-byte far call (ldr.w lr + blx lr). */
#define generate_branch_update(writeback_location, new_pc, mode)              \
  t2_load_imm32(reg_gpc, new_pc);                                             \
  t2_mov_reg_shift(0, reg_a0, reg_cycles, T2SHIFT_LSR, 31);                   \
  t2_cbnz(reg_a0, 4);                                                         \
  generate_function_far_call(armfn_gbaup_##mode);                             \
  generate_branch_filler(0x0E, writeback_location)                            \

#define generate_branch_no_cycle_update(writeback_location, new_pc, mode)     \
  if(pc == idle_loop_target_pc)                                               \
  {                                                                           \
    generate_branch_idle_eliminate(writeback_location, new_pc, mode);         \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    generate_branch_update(writeback_location, new_pc, mode);                 \
  }                                                                           \

#define generate_branch_cycle_update(writeback_location, new_pc, mode)        \
  generate_cycle_update();                                                    \
  generate_branch_no_cycle_update(writeback_location, new_pc, mode)           \

/* a0 holds the destination */

#define generate_indirect_branch_no_cycle_update(type)                        \
  t2_ldr_imm(T2REG_PC, reg_base, 4*(REG_USERDEF + armfn_indirect_##type))     \

#define generate_indirect_branch_cycle_update(type)                           \
  generate_cycle_update();                                                    \
  generate_indirect_branch_no_cycle_update(type)                              \

#define generate_indirect_branch_arm()                                        \
  {                                                                           \
    if(condition == 0x0E)                                                     \
    {                                                                         \
      generate_cycle_update();                                                \
    }                                                                         \
    generate_indirect_branch_no_cycle_update(arm);                            \
  }                                                                           \

#define generate_indirect_branch_dual()                                       \
  {                                                                           \
    if(condition == 0x0E)                                                     \
    {                                                                         \
      generate_cycle_update();                                                \
    }                                                                         \
    generate_indirect_branch_no_cycle_update(dual_arm);                       \
  }                                                                           \

#define generate_load_memreg(ireg, reg_index)                                 \
  t2_ldr_imm(ireg, reg_base, ((reg_index) * 4))                               \

#define generate_store_memreg(ireg, reg_index)                                \
  t2_str_imm(ireg, reg_base, ((reg_index) * 4))                               \

#define arm_generate_store_reg(ireg, reg_index)                               \
{                                                                             \
  u32 store_dest = arm_register_allocation[reg_index];                        \
  if(store_dest != mem_reg)                                                   \
  {                                                                           \
    generate_mov(store_dest, ireg);                                           \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    generate_store_memreg(ireg, reg_index);                                   \
  }                                                                           \
}                                                                             \

#define thumb_generate_store_reg(ireg, reg_index)                             \
{                                                                             \
  u32 store_dest = thumb_register_allocation[reg_index];                      \
  if(store_dest != mem_reg)                                                   \
  {                                                                           \
    generate_mov(store_dest, ireg);                                           \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    generate_store_memreg(ireg, reg_index);                                   \
  }                                                                           \
}

#define arm_generate_load_reg(ireg, reg_index)                                \
{                                                                             \
  u32 load_src = arm_register_allocation[reg_index];                          \
  if(load_src != mem_reg)                                                     \
  {                                                                           \
    generate_mov(ireg, load_src);                                             \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    generate_load_memreg(ireg, reg_index);                                    \
  }                                                                           \
}                                                                             \

#define thumb_generate_load_reg(ireg, reg_index)                              \
{                                                                             \
  u32 load_src = thumb_register_allocation[reg_index];                        \
  if(load_src != mem_reg)                                                     \
  {                                                                           \
    generate_mov(ireg, load_src);                                             \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    generate_load_memreg(ireg, reg_index);                                    \
  }                                                                           \
}                                                                             \

#define arm_generate_load_reg_pc(ireg, reg_index, pc_offset)                  \
  if(reg_index == 15)                                                         \
  {                                                                           \
    generate_load_pc(ireg, pc + pc_offset);                                   \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    arm_generate_load_reg(ireg, reg_index);                                   \
  }                                                                           \

#define thumb_generate_load_reg_pc(ireg, reg_index, pc_offset)                \
  if(reg_index == 15)                                                         \
  {                                                                           \
    generate_load_pc(ireg, pc + pc_offset);                                   \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    thumb_generate_load_reg(ireg, reg_index);                                 \
  }                                                                           \

u32 arm_prepare_store_reg(u32 scratch_reg, u32 reg_index)
{
  u32 reg_use = arm_register_allocation[reg_index];
  if(reg_use == mem_reg)
    return scratch_reg;

  return reg_use;
}

u32 thumb_prepare_store_reg(u32 scratch_reg, u32 reg_index)
{
  u32 reg_use = thumb_register_allocation[reg_index];
  if(reg_use == mem_reg)
    return scratch_reg;

  return reg_use;
}

u32 arm_prepare_load_reg(u8 **tptr, u32 scratch_reg, u32 reg_index)
{
  u32 reg_use = arm_register_allocation[reg_index];
  if(reg_use != mem_reg)
    return reg_use;

  u8 *translation_ptr = *tptr;
  t2_ldr_imm(scratch_reg, reg_base, (reg_index * 4));
  *tptr = translation_ptr;
  return scratch_reg;
}

u32 arm_prepare_load_reg_pc(u8 **tptr, u32 scratch_reg, u32 reg_index, u32 pc_value)
{
  if(reg_index == 15)
  {
    u8 *translation_ptr = *tptr;
    generate_load_pc(scratch_reg, pc_value);
    *tptr = translation_ptr;
    return scratch_reg;
  }
  return arm_prepare_load_reg(tptr, scratch_reg, reg_index);
}

u32 thumb_prepare_load_reg(u8 **tptr, u32 scratch_reg, u32 reg_index)
{
  u32 reg_use = thumb_register_allocation[reg_index];
  if(reg_use != mem_reg)
    return reg_use;

  u8 *translation_ptr = *tptr;
  t2_ldr_imm(scratch_reg, reg_base, (reg_index * 4));
  *tptr = translation_ptr;
  return scratch_reg;
}

u32 thumb_prepare_load_reg_pc(u8 **tptr, u32 scratch_reg, u32 reg_index, u32 pc_value)
{
  if(reg_index == 15)
  {
    u8 *translation_ptr = *tptr;
    generate_load_pc(scratch_reg, pc_value);
    *tptr = translation_ptr;
    return scratch_reg;
  }
  return thumb_prepare_load_reg(tptr, scratch_reg, reg_index);
}

#define arm_complete_store_reg(scratch_reg, reg_index)                        \
{                                                                             \
  if(arm_register_allocation[reg_index] == mem_reg)                           \
  {                                                                           \
    generate_store_memreg(scratch_reg, reg_index);                            \
  }                                                                           \
}

#define thumb_complete_store_reg(scratch_reg, reg_index)                      \
{                                                                             \
  if(thumb_register_allocation[reg_index] == mem_reg)                         \
  {                                                                           \
    generate_store_memreg(scratch_reg, reg_index);                            \
  }                                                                           \
}

#define block_prologue_size 0
#define generate_block_prologue()

/* T32 emits 16-bit forms, so a block can end 2-byte aligned; the frontend
 * writes 8-byte block headers into the stream with STRD (4-byte alignment
 * required on M7). Re-align at every block boundary. */
#define align_translation_ptr()                                               \
  translation_ptr = (u8 *)(((uintptr_t)translation_ptr + 3) & ~(uintptr_t)3)

#define generate_block_extra_vars_arm()
#define generate_block_extra_vars_thumb()

#define arm_complete_store_reg_pc_no_flags(scratch_reg, reg_index)            \
{                                                                             \
  if(reg_index == 15)                                                         \
  {                                                                           \
    generate_indirect_branch_arm();                                           \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    arm_complete_store_reg(scratch_reg, reg_index);                           \
  }                                                                           \
}                                                                             \

#define arm_complete_store_reg_pc_flags(scratch_reg, reg_index)               \
{                                                                             \
  if(reg_index == 15)                                                         \
  {                                                                           \
    if(condition == 0x0E)                                                     \
    {                                                                         \
      generate_cycle_update();                                                \
    }                                                                         \
    generate_function_far_call(armfn_spsr_restore);                           \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    arm_complete_store_reg(scratch_reg, reg_index);                           \
  }                                                                           \
}                                                                             \

#define check_for_interrupts()                                                \
  if((io_registers[REG_IE] & io_registers[REG_IF]) &&                         \
   io_registers[REG_IME] && ((reg[REG_CPSR] & 0x80) == 0))                    \
  {                                                                           \
    REG_MODE(MODE_IRQ)[6] = pc + 4;                                           \
    REG_SPSR(MODE_IRQ) = reg[REG_CPSR];                                       \
    reg[REG_CPSR] = 0xD2;                                                     \
    pc = 0x00000018;                                                          \
    set_cpu_mode(MODE_IRQ);                                                   \
  }                                                                           \

#define arm_generate_store_reg_pc_no_flags(ireg, reg_index)                   \
  arm_generate_store_reg(ireg, reg_index);                                    \
  if(reg_index == 15)                                                         \
  {                                                                           \
    generate_indirect_branch_arm();                                           \
  }                                                                           \

u32 execute_spsr_restore_body(u32 pc)
{
  set_cpu_mode(cpu_modes[reg[REG_CPSR] & 0xF]);
  check_for_interrupts();

  return pc;
}

#define generate_save_flags()                                                 \
  t2_mrs_apsr(reg_flags)                                                      \

#define generate_restore_flags()                                              \
  t2_msr_apsr(reg_flags)                                                      \

#define condition_opposite_eq T2_CC_NE
#define condition_opposite_ne T2_CC_EQ
#define condition_opposite_cs T2_CC_CC
#define condition_opposite_cc T2_CC_CS
#define condition_opposite_mi T2_CC_PL
#define condition_opposite_pl T2_CC_MI
#define condition_opposite_vs T2_CC_VC
#define condition_opposite_vc T2_CC_VS
#define condition_opposite_hi T2_CC_LS
#define condition_opposite_ls T2_CC_HI
#define condition_opposite_ge T2_CC_LT
#define condition_opposite_lt T2_CC_GE
#define condition_opposite_gt T2_CC_LE
#define condition_opposite_le T2_CC_GT
#define condition_opposite_al 0x0F
#define condition_opposite_nv T2_CC_AL

#define generate_branch(mode)                                                 \
{                                                                             \
  generate_branch_cycle_update(                                               \
   block_exits[block_exit_position].branch_source,                            \
   block_exits[block_exit_position].branch_target, mode);                     \
  block_exit_position++;                                                      \
}                                                                             \

/* Register-immshift ALU ops map 1:1 to T32 shifted-register forms. */

#define generate_op_and_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_AND, 0, _rd, _rn, _rm, st, shift)                            \

#define generate_op_orr_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_ORR, 0, _rd, _rn, _rm, st, shift)                            \

#define generate_op_eor_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_EOR, 0, _rd, _rn, _rm, st, shift)                            \

#define generate_op_bic_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_BIC, 0, _rd, _rn, _rm, st, shift)                            \

#define generate_op_sub_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_SUB, 0, _rd, _rn, _rm, st, shift)                            \

#define generate_op_rsb_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_RSB, 0, _rd, _rn, _rm, st, shift)                            \

#define generate_op_sbc_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_SBC, 0, _rd, _rn, _rm, st, shift)                            \

/* RSC rd,rn,op2 == ADC(rd, ~rn, op2); r10 holds ~rn. */
#define generate_op_rsc_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_mvn_reg_shift(0, reg_gpc, _rn, T2SHIFT_LSL, 0);                          \
  t2_dp_reg(T2OP_ADC, 0, _rd, reg_gpc, _rm, st, shift)                        \

#define generate_op_add_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_ADD, 0, _rd, _rn, _rm, st, shift)                            \

#define generate_op_adc_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_ADC, 0, _rd, _rn, _rm, st, shift)                            \

#define generate_op_mov_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_mov_reg_shift(0, _rd, _rm, st, shift)                                    \

#define generate_op_mvn_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_mvn_reg_shift(0, _rd, _rm, st, shift)                                    \

/* Register-regshift: T32 has no register-shifted ALU operands; shift into
 * r10 first. For flag-setting logical ops the shift is done with S (C from
 * the shifter), and the following register ALU op leaves C untouched. */

#define t2_regshift_prep(s, _rm, st, _rs)                                     \
  t2_shift_reg(s, st, reg_gpc, _rm, _rs)                                      \

#define generate_op_and_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_AND, 0, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_orr_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_ORR, 0, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_eor_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_EOR, 0, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_bic_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_BIC, 0, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_sub_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_SUB, 0, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_rsb_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_RSB, 0, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_sbc_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_SBC, 0, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_rsc_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_mvn_reg_shift(0, reg_rs, _rn, T2SHIFT_LSL, 0);                           \
  t2_dp_reg(T2OP_ADC, 0, _rd, reg_rs, reg_gpc, T2SHIFT_LSL, 0)                \

#define generate_op_add_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_ADD, 0, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_adc_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_ADC, 0, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_mov_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_shift_reg(0, st, _rd, _rm, _rs)                                          \

#define generate_op_mvn_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_mvn_reg_shift(0, _rd, reg_gpc, T2SHIFT_LSL, 0)                           \

/* Immediate forms: `imm`/`imm_ror` are in scope at the call sites. */

#define generate_op_and_imm(_rd, _rn)                                         \
  t2_dp_imm_auto(T2OP_AND, 0, _rd, _rn, t2_rot_value(imm, imm_ror))           \

#define generate_op_orr_imm(_rd, _rn)                                         \
  t2_dp_imm_auto(T2OP_ORR, 0, _rd, _rn, t2_rot_value(imm, imm_ror))           \

#define generate_op_eor_imm(_rd, _rn)                                         \
  t2_dp_imm_auto(T2OP_EOR, 0, _rd, _rn, t2_rot_value(imm, imm_ror))           \

#define generate_op_bic_imm(_rd, _rn)                                         \
  t2_dp_imm_auto(T2OP_BIC, 0, _rd, _rn, t2_rot_value(imm, imm_ror))           \

#define generate_op_sub_imm(_rd, _rn)                                         \
  t2_dp_imm_auto(T2OP_SUB, 0, _rd, _rn, t2_rot_value(imm, imm_ror))           \

#define generate_op_rsb_imm(_rd, _rn)                                         \
  t2_dp_imm_auto(T2OP_RSB, 0, _rd, _rn, t2_rot_value(imm, imm_ror))           \

#define generate_op_sbc_imm(_rd, _rn)                                         \
  t2_dp_imm_auto(T2OP_SBC, 0, _rd, _rn, t2_rot_value(imm, imm_ror))           \

/* ~rn goes in r14: the imm fallback path uses r10 itself. */
#define generate_op_rsc_imm(_rd, _rn)                                         \
  t2_mvn_reg_shift(0, reg_rs, _rn, T2SHIFT_LSL, 0);                           \
  t2_dp_imm_auto(T2OP_ADC, 0, _rd, reg_rs, t2_rot_value(imm, imm_ror))        \

#define generate_op_add_imm(_rd, _rn)                                         \
  t2_dp_imm_auto(T2OP_ADD, 0, _rd, _rn, t2_rot_value(imm, imm_ror))           \

#define generate_op_adc_imm(_rd, _rn)                                         \
  t2_dp_imm_auto(T2OP_ADC, 0, _rd, _rn, t2_rot_value(imm, imm_ror))           \

#define generate_op_mov_imm(_rd, _rn)                                         \
  generate_load_imm(_rd, imm, imm_ror)                                        \

#define generate_op_mvn_imm(_rd, _rn)                                         \
{                                                                             \
  u32 _t2mv = ~t2_rot_value(imm, imm_ror);                                    \
  u32 _t2me = t2_imm12(_t2mv);                                                \
  if (_t2me != 0xFFFFFFFF)                                                    \
  {                                                                           \
    t2_dp_imm(T2OP_ORR, 0, _rd, 15, _t2me);   /* mov rd, #~v */               \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    t2_load_imm32(_rd, _t2mv);                                                \
  }                                                                           \
}                                                                             \

/* --- Flag-setting variants -------------------------------------------------
 * lflags (logical): N/Z from result, C from the shifter -> shift with S,
 * ALU op with S and no shift (C preserved). aflags (arithmetic): NZCV from
 * the operation, shifter carry irrelevant. uflags: MOVS/MVNS. tflags:
 * TST/TEQ (logical) and CMP/CMN (arithmetic). */

#define generate_op_ands_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_dp_reg(T2OP_AND, 1, _rd, _rn, _rm, st, shift)                            \

#define generate_op_orrs_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_dp_reg(T2OP_ORR, 1, _rd, _rn, _rm, st, shift)                            \

#define generate_op_eors_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_dp_reg(T2OP_EOR, 1, _rd, _rn, _rm, st, shift)                            \

#define generate_op_bics_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_dp_reg(T2OP_BIC, 1, _rd, _rn, _rm, st, shift)                            \

#define generate_op_subs_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_dp_reg(T2OP_SUB, 1, _rd, _rn, _rm, st, shift)                            \

#define generate_op_rsbs_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_dp_reg(T2OP_RSB, 1, _rd, _rn, _rm, st, shift)                            \

#define generate_op_sbcs_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_dp_reg(T2OP_SBC, 1, _rd, _rn, _rm, st, shift)                            \

/* RSCS == ADCS(rd, ~rn, op2): correct NZCV for the reverse subtraction. */
#define generate_op_rscs_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_mvn_reg_shift(0, reg_gpc, _rn, T2SHIFT_LSL, 0);                          \
  t2_dp_reg(T2OP_ADC, 1, _rd, reg_gpc, _rm, st, shift)                        \

#define generate_op_adds_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_dp_reg(T2OP_ADD, 1, _rd, _rn, _rm, st, shift)                            \

#define generate_op_adcs_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_dp_reg(T2OP_ADC, 1, _rd, _rn, _rm, st, shift)                            \

#define generate_op_movs_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_mov_reg_shift(1, _rd, _rm, st, shift)                                    \

#define generate_op_mvns_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_mvn_reg_shift(1, _rd, _rm, st, shift)                                    \

/* The reg operand is in reg_rm, not reg_rn like expected. */
#define generate_op_neg_reg_immshift(_rd, _rn, _rm, st, shift)                \
{                                                                             \
  t2_dp_imm(T2OP_RSB, 1, _rd, _rm, 0);      /* rsbs rd, rm, #0 */             \
}                                                                             \

#define generate_op_muls_reg_immshift(_rd, _rn, _rm, st, shift)               \
  t2_mul(_rd, _rn, _rm);                                                      \
  t2_mov_reg_shift(1, _rd, _rd, T2SHIFT_LSL, 0)  /* movs rd, rd: N/Z */       \

#define generate_op_cmp_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_SUB, 1, 15, _rn, _rm, st, shift)                             \

#define generate_op_cmn_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_ADD, 1, 15, _rn, _rm, st, shift)                             \

#define generate_op_tst_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_AND, 1, 15, _rn, _rm, st, shift)                             \

#define generate_op_teq_reg_immshift(_rd, _rn, _rm, st, shift)                \
  t2_dp_reg(T2OP_EOR, 1, 15, _rn, _rm, st, shift)                             \

#define generate_op_ands_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(1, _rm, st, _rs);          /* C from shifter */            \
  t2_dp_reg(T2OP_AND, 1, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_orrs_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(1, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_ORR, 1, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_eors_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(1, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_EOR, 1, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_bics_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(1, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_BIC, 1, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_subs_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_SUB, 1, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_rsbs_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_RSB, 1, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_sbcs_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_SBC, 1, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_rscs_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_mvn_reg_shift(0, reg_rs, _rn, T2SHIFT_LSL, 0);                           \
  t2_dp_reg(T2OP_ADC, 1, _rd, reg_rs, reg_gpc, T2SHIFT_LSL, 0)                \

#define generate_op_adds_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_ADD, 1, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_adcs_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_ADC, 1, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0)                   \

#define generate_op_movs_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_shift_reg(1, st, _rd, _rm, _rs)                                          \

#define generate_op_mvns_reg_regshift(_rd, _rn, _rm, st, _rs)                 \
  t2_regshift_prep(1, _rm, st, _rs);                                          \
  t2_mvn_reg_shift(1, _rd, reg_gpc, T2SHIFT_LSL, 0)                           \

#define generate_op_cmp_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_SUB, 1, 15, _rn, reg_gpc, T2SHIFT_LSL, 0)                    \

#define generate_op_cmn_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(0, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_ADD, 1, 15, _rn, reg_gpc, T2SHIFT_LSL, 0)                    \

#define generate_op_tst_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(1, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_AND, 1, 15, _rn, reg_gpc, T2SHIFT_LSL, 0)                    \

#define generate_op_teq_reg_regshift(_rd, _rn, _rm, st, _rs)                  \
  t2_regshift_prep(1, _rm, st, _rs);                                          \
  t2_dp_reg(T2OP_EOR, 1, 15, _rn, reg_gpc, T2SHIFT_LSL, 0)                    \

/* Flag-setting immediates: T32 modified-immediate S-forms set C from the
 * immediate's rotation exactly like ARM, so logical S-ops map directly.
 * The r10 fallback would lose the shifter carry, but ARM-encodable guest
 * immediates never take it. */

#define generate_op_s_imm(op, _rd, _rn)                                       \
{                                                                             \
  u32 _t2v = t2_rot_value(imm, imm_ror);                                      \
  u32 _t2e = t2_imm12(_t2v);                                                  \
  if (_t2e != 0xFFFFFFFF)                                                     \
  {                                                                           \
    t2_dp_imm(op, 1, _rd, _rn, _t2e);                                         \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    t2_load_imm32(reg_gpc, _t2v);                                             \
    t2_dp_reg(op, 1, _rd, _rn, reg_gpc, T2SHIFT_LSL, 0);                      \
  }                                                                           \
}                                                                             \

#define generate_op_ands_imm(_rd, _rn) generate_op_s_imm(T2OP_AND, _rd, _rn)
#define generate_op_orrs_imm(_rd, _rn) generate_op_s_imm(T2OP_ORR, _rd, _rn)
#define generate_op_eors_imm(_rd, _rn) generate_op_s_imm(T2OP_EOR, _rd, _rn)
#define generate_op_bics_imm(_rd, _rn) generate_op_s_imm(T2OP_BIC, _rd, _rn)
#define generate_op_subs_imm(_rd, _rn) generate_op_s_imm(T2OP_SUB, _rd, _rn)
#define generate_op_rsbs_imm(_rd, _rn) generate_op_s_imm(T2OP_RSB, _rd, _rn)
#define generate_op_sbcs_imm(_rd, _rn) generate_op_s_imm(T2OP_SBC, _rd, _rn)
#define generate_op_adds_imm(_rd, _rn) generate_op_s_imm(T2OP_ADD, _rd, _rn)
#define generate_op_adcs_imm(_rd, _rn) generate_op_s_imm(T2OP_ADC, _rd, _rn)

#define generate_op_rscs_imm(_rd, _rn)                                        \
  t2_mvn_reg_shift(0, reg_rs, _rn, T2SHIFT_LSL, 0);                           \
  generate_op_s_imm(T2OP_ADC, _rd, reg_rs)                                    \

#define generate_op_movs_imm(_rd, _rn)                                        \
{                                                                             \
  u32 _t2v = t2_rot_value(imm, imm_ror);                                      \
  u32 _t2e = t2_imm12(_t2v);                                                  \
  if (_t2e != 0xFFFFFFFF)                                                     \
  {                                                                           \
    t2_dp_imm(T2OP_ORR, 1, _rd, 15, _t2e);                                    \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    t2_load_imm32(_rd, _t2v);                                                 \
    t2_mov_reg_shift(1, _rd, _rd, T2SHIFT_LSL, 0);                            \
  }                                                                           \
}                                                                             \

#define generate_op_mvns_imm(_rd, _rn)                                        \
{                                                                             \
  u32 _t2v = t2_rot_value(imm, imm_ror);                                      \
  u32 _t2e = t2_imm12(_t2v);                                                  \
  if (_t2e != 0xFFFFFFFF)                                                     \
  {                                                                           \
    t2_dp_imm(T2OP_ORN, 1, _rd, 15, _t2e);                                    \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    t2_load_imm32(reg_gpc, _t2v);                                             \
    t2_mvn_reg_shift(1, _rd, reg_gpc, T2SHIFT_LSL, 0);                        \
  }                                                                           \
}                                                                             \

#define generate_op_cmp_imm(_rd, _rn) generate_op_s_imm(T2OP_SUB, 15, _rn)
#define generate_op_cmn_imm(_rd, _rn) generate_op_s_imm(T2OP_ADD, 15, _rn)
#define generate_op_tst_imm(_rd, _rn) generate_op_s_imm(T2OP_AND, 15, _rn)
#define generate_op_teq_imm(_rd, _rn) generate_op_s_imm(T2OP_EOR, 15, _rn)

/* --- Data-processing translation drivers (same as the ARM backend) -------- */

#define arm_prepare_load_rn_yes()                                             \
  u32 _rn = arm_prepare_load_reg_pc(&translation_ptr, reg_rn, rn, pc + 8)     \

#define arm_prepare_load_rn_no()                                              \

#define arm_prepare_store_rd_yes()                                            \
  u32 _rd = arm_prepare_store_reg(reg_rd, rd)                                 \

#define arm_prepare_store_rd_no()                                             \

#define arm_complete_store_rd_yes(flags_op)                                   \
  arm_complete_store_reg_pc_##flags_op(_rd, rd)                               \

#define arm_complete_store_rd_no(flags_op)                                    \

#define arm_generate_op_reg(name, load_op, store_op, flags_op)                \
  u32 shift_type = (opcode >> 5) & 0x03;                                      \
  arm_decode_data_proc_reg(opcode);                                           \
  arm_prepare_load_rn_##load_op();                                            \
  arm_prepare_store_rd_##store_op();                                          \
                                                                              \
  if((opcode >> 4) & 0x01)                                                    \
  {                                                                           \
    u32 rs = ((opcode >> 8) & 0x0F);                                          \
    u32 _rs = arm_prepare_load_reg(&translation_ptr, reg_rs, rs);             \
    u32 _rm = arm_prepare_load_reg_pc(&translation_ptr, reg_rm, rm, pc + 12); \
    generate_op_##name##_reg_regshift(_rd, _rn, _rm, shift_type, _rs);        \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    u32 shift_imm = ((opcode >> 7) & 0x1F);                                   \
    u32 _rm = arm_prepare_load_reg_pc(&translation_ptr, reg_rm, rm, pc + 8);  \
    generate_op_##name##_reg_immshift(_rd, _rn, _rm, shift_type, shift_imm);  \
  }                                                                           \
  arm_complete_store_rd_##store_op(flags_op)                                  \

#define arm_generate_op_reg_flags(name, load_op, store_op, flags_op)          \
  arm_generate_op_reg(name, load_op, store_op, flags_op)                      \

/* imm will be loaded by the called function if necessary. */

#define arm_generate_op_imm(name, load_op, store_op, flags_op)                \
  arm_decode_data_proc_imm(opcode);                                           \
  arm_prepare_load_rn_##load_op();                                            \
  arm_prepare_store_rd_##store_op();                                          \
  generate_op_##name##_imm(_rd, _rn);                                         \
  arm_complete_store_rd_##store_op(flags_op)                                  \

#define arm_generate_op_imm_flags(name, load_op, store_op, flags_op)          \
  arm_generate_op_imm(name, load_op, store_op, flags_op)                      \

#define arm_data_proc(name, type, flags_op)                                   \
{                                                                             \
  arm_generate_op_##type(name, yes, yes, flags_op);                           \
}                                                                             \

#define arm_data_proc_test(name, type)                                        \
{                                                                             \
  arm_generate_op_##type(name, yes, no, no);                                  \
}                                                                             \

#define arm_data_proc_unary(name, type, flags_op)                             \
{                                                                             \
  arm_generate_op_##type(name, no, yes, flags_op);                            \
}                                                                             \

/* --- Multiplies ------------------------------------------------------------
 * T32 has no S-suffix multiplies: N/Z come from a following MOVS (C is
 * UNPREDICTABLE after ARMv4 MULS anyway). Long multiplies use ORRS into
 * r10: Z exact, N approximated (lo31|hi31) - see header comment. */

#define arm_multiply_add_no_flags_no()                                        \
  t2_mul(_rd, _rm, _rs)                                                       \

#define arm_multiply_add_yes_flags_no()                                       \
  u32 _rn = arm_prepare_load_reg(&translation_ptr, reg_a2, rn);               \
  t2_mla(_rd, _rm, _rs, _rn)                                                  \

#define arm_multiply_add_no_flags_yes()                                       \
  t2_mul(_rd, _rm, _rs);                                                      \
  t2_mov_reg_shift(1, _rd, _rd, T2SHIFT_LSL, 0)                               \

#define arm_multiply_add_yes_flags_yes()                                      \
  u32 _rn = arm_prepare_load_reg(&translation_ptr, reg_a2, rn);               \
  t2_mla(_rd, _rm, _rs, _rn);                                                 \
  t2_mov_reg_shift(1, _rd, _rd, T2SHIFT_LSL, 0)                               \

#define arm_multiply(add_op, flags)                                           \
{                                                                             \
  arm_decode_multiply();                                                      \
  u32 _rm = arm_prepare_load_reg(&translation_ptr, reg_a0, rm);               \
  u32 _rs = arm_prepare_load_reg(&translation_ptr, reg_a1, rs);               \
  u32 _rd = arm_prepare_store_reg(reg_a0, rd);                                \
  arm_multiply_add_##add_op##_flags_##flags();                                \
  arm_complete_store_reg(_rd, rd);                                            \
}                                                                             \

#define arm_multiply_long_name_s64     t2_smull
#define arm_multiply_long_name_u64     t2_umull
#define arm_multiply_long_name_s64_add t2_smlal
#define arm_multiply_long_name_u64_add t2_umlal

#define arm_multiply_long_flags_no(name)                                      \
  name(_rdlo, _rdhi, _rm, _rs)                                                \

#define arm_multiply_long_flags_yes(name)                                     \
  name(_rdlo, _rdhi, _rm, _rs);                                               \
  t2_dp_reg(T2OP_ORR, 1, reg_gpc, _rdlo, _rdhi, T2SHIFT_LSL, 0)               \

#define arm_multiply_long_add_no(name)                                        \

#define arm_multiply_long_add_yes(name)                                       \
  arm_prepare_load_reg(&translation_ptr, reg_a0, rdlo);                       \
  arm_prepare_load_reg(&translation_ptr, reg_a1, rdhi)                        \

#define arm_multiply_long_op(flags, name)                                     \
  arm_multiply_long_flags_##flags(name)                                       \

#define arm_multiply_long(name, add_op, flags)                                \
{                                                                             \
  arm_decode_multiply_long();                                                 \
  u32 _rm = arm_prepare_load_reg(&translation_ptr, reg_a2, rm);               \
  u32 _rs = arm_prepare_load_reg(&translation_ptr, reg_rs, rs);               \
  u32 _rdlo = (rdlo == rdhi) ? reg_a0 : arm_prepare_store_reg(reg_a0, rdlo);  \
  u32 _rdhi = arm_prepare_store_reg(reg_a1, rdhi);                            \
  arm_multiply_long_add_##add_op(name);                                       \
  arm_multiply_long_op(flags, arm_multiply_long_name_##name);                 \
  arm_complete_store_reg(_rdlo, rdlo);                                        \
  arm_complete_store_reg(_rdhi, rdhi);                                        \
}                                                                             \

/* --- PSR transfers ---------------------------------------------------------- */

#define arm_psr_read_cpsr()                                                   \
{                                                                             \
  u32 _rd = arm_prepare_store_reg(reg_a0, rd);                                \
  generate_load_memreg(_rd, REG_CPSR);                                        \
  generate_save_flags();                                                      \
  t2_dp_imm_auto(T2OP_BIC, 0, _rd, _rd, 0xF0000000);                          \
  t2_dp_imm_auto(T2OP_AND, 0, reg_flags, reg_flags, 0xF0000000);              \
  t2_dp_reg(T2OP_ORR, 0, _rd, _rd, reg_flags, T2SHIFT_LSL, 0);                \
  arm_complete_store_reg(_rd, rd)                                             \
}

#define arm_psr_read_spsr()                                                   \
{                                                                             \
  u32 _rd = arm_prepare_store_reg(reg_a0, rd);                                \
  t2_addw(reg_a0, reg_base, SPSR_RAM_OFF);                                    \
  generate_load_memreg(reg_a1, CPU_MODE);                                     \
  t2_dp_imm_auto(T2OP_AND, 0, reg_a1, reg_a1, 0xF);                           \
  t2_ldr_reg(_rd, reg_a0, reg_a1, 2);                                         \
  arm_complete_store_reg(_rd, rd);                                            \
}

#define arm_psr_read(op_type, psr_reg)                                        \
  arm_psr_read_##psr_reg()                                                    \

/* This function's okay because it's called from an ASM function that can
 * wrap it correctly. */
u32 execute_store_cpsr_body(u32 _cpsr, u32 store_mask, u32 address)
{
  reg[REG_CPSR] = _cpsr;
  if(store_mask & 0xFF)
  {
    set_cpu_mode(cpu_modes[_cpsr & 0xF]);
    if((io_registers[REG_IE] & io_registers[REG_IF]) &&
     io_registers[REG_IME] && ((_cpsr & 0x80) == 0))
    {
      REG_MODE(MODE_IRQ)[6] = address + 4;
      REG_SPSR(MODE_IRQ) = _cpsr;
      reg[REG_CPSR] = 0xD2;
      set_cpu_mode(MODE_IRQ);
      return 0x00000018;
    }
  }

  return 0;
}

static void trace_instruction(u32 pc, u32 mode)
{
  if (mode)
    printf("Executed arm %x\n", pc);
  else
    printf("Executed thumb %x\n", pc);
  #ifdef TRACE_REGISTERS
  print_regs();
  #endif
}

#ifdef TRACE_INSTRUCTIONS
  #define emit_trace_instruction(pc, mode, regt)   \
  {                                                \
    unsigned i;                                    \
    for (i = 0; i < 15; i++) {                     \
      if (regt[i] != mem_reg) {                    \
        generate_store_memreg(regt[i], i);         \
      }                                            \
    }                                              \
    generate_save_flags();                         \
    t2_push(0x500C);                               \
    generate_load_pc(reg_a0, pc);                  \
    generate_load_pc(reg_a1, mode);                \
    generate_function_far_call(armfn_debug_trace); \
    t2_pop(0x500C);                                \
    generate_restore_flags();                      \
  }

  #define emit_trace_thumb_instruction(pc)         \
    emit_trace_instruction(pc, 0, thumb_register_allocation)

  #define emit_trace_arm_instruction(pc)           \
    emit_trace_instruction(pc, 1, arm_register_allocation)
#else
  #define emit_trace_thumb_instruction(pc)
  #define emit_trace_arm_instruction(pc)
#endif

#define arm_psr_load_new_reg()                                                \
  arm_generate_load_reg(reg_a0, rm)                                           \

#define arm_psr_load_new_imm()                                                \
  generate_load_imm(reg_a0, imm, imm_ror)                                     \

/* Thumb-2 stub ABI: masks in r1 (user) / r2 (privileged), guest PC in r10. */
#define arm_psr_store_cpsr()                                                  \
  t2_load_imm32(reg_a1, cpsr_masks[psr_pfield][0]);                           \
  t2_load_imm32(reg_a2, cpsr_masks[psr_pfield][1]);                           \
  t2_load_imm32(reg_gpc, pc);                                                 \
  generate_function_far_call(armfn_store_cpsr)                                \

#define arm_psr_store_spsr()                                                  \
  t2_load_imm32(reg_a1, spsr_masks[psr_pfield]);                              \
  generate_load_memreg(reg_a2, CPU_MODE);                                     \
  t2_dp_imm_auto(T2OP_AND, 0, reg_a2, reg_a2, 0xF);                           \
  t2_dp_reg(T2OP_ADD, 0, T2REG_LR, reg_base, reg_a2, T2SHIFT_LSL, 2);         \
  t2_dp_reg(T2OP_AND, 0, reg_a0, reg_a0, reg_a1, T2SHIFT_LSL, 0);             \
  t2_ldr_imm(reg_a2, T2REG_LR, SPSR_RAM_OFF);                                 \
  t2_dp_reg(T2OP_BIC, 0, reg_a2, reg_a2, reg_a1, T2SHIFT_LSL, 0);             \
  t2_dp_reg(T2OP_ORR, 0, reg_a0, reg_a0, reg_a2, T2SHIFT_LSL, 0);             \
  t2_str_imm(reg_a0, T2REG_LR, SPSR_RAM_OFF)                                  \

#define arm_psr_store(op_type, psr_reg)                                       \
  arm_psr_load_new_##op_type();                                               \
  arm_psr_store_##psr_reg()                                                   \

#define arm_psr(op_type, transfer_type, psr_reg)                              \
{                                                                             \
  arm_decode_psr_##op_type(opcode);                                           \
  arm_psr_##transfer_type(op_type, psr_reg);                                  \
}                                                                             \

/* --- Memory access -----------------------------------------------------------
 * Address -> handler-table region index via USAT (region 0..15 saturated,
 * ROR pre-fold for alignment mirroring, same trick as the ARMv6+ backend).
 * The handler is fetched from the tables at reg_base+STORE_TBL_OFF and
 * BLX'd; the guest PC rides in r10 (no inline literals - see the stub). */

#define mem_calc_region(abits)                                                \
  if (abits) {                                                                \
    t2_mov_reg_shift(0, reg_a2, reg_a0, T2SHIFT_ROR, abits);                  \
    t2_usat_asr(reg_a2, 4, reg_a2, 24-(abits));                               \
  } else {                                                                    \
    t2_usat_asr(reg_a2, 4, reg_a0, 24);                                       \
  }

#define generate_load_call_byte(tblnum, pcval)                                \
  t2_load_imm32(reg_gpc, pcval);                                              \
  mem_calc_region(0);                                                         \
  t2_addw(reg_a2, reg_a2, (STORE_TBL_OFF + 68*tblnum + 4) >> 2);              \
  t2_ldr_reg(reg_a2, reg_base, reg_a2, 2);                                    \
  t2_blx(reg_a2)                                                              \

#define generate_load_call_mbyte(tblnum, abits, pcval)                        \
  t2_load_imm32(reg_gpc, pcval);                                              \
  mem_calc_region(abits);                                                     \
  t2_addw(reg_a2, reg_a2, (STORE_TBL_OFF + 68*tblnum + 4) >> 2);              \
  t2_ldr_reg(reg_a2, reg_base, reg_a2, 2);                                    \
  t2_blx(reg_a2)                                                              \

#define generate_store_call(tblnum, pcval)                                    \
  t2_load_imm32(reg_gpc, pcval);                                              \
  mem_calc_region(0);                                                         \
  t2_addw(reg_a2, reg_a2, (STORE_TBL_OFF + 68*tblnum + 4) >> 2);              \
  t2_ldr_reg(reg_a2, reg_base, reg_a2, 2);                                    \
  t2_blx(reg_a2)                                                              \

#define generate_store_call_u8(pcval)        generate_store_call(0, pcval)
#define generate_store_call_u16(pcval)       generate_store_call(1, pcval)
#define generate_store_call_u32(pcval)       generate_store_call(2, pcval)
#define generate_store_call_u32_safe(pcval)  generate_store_call(3, pcval)
#define generate_load_call_u8(pcval)         generate_load_call_byte(4, pcval)
#define generate_load_call_s8(pcval)         generate_load_call_byte(5, pcval)
#define generate_load_call_u16(pcval)        generate_load_call_mbyte(6, 1, pcval)
#define generate_load_call_s16(pcval)        generate_load_call_mbyte(7, 1, pcval)
#define generate_load_call_u32(pcval)        generate_load_call_mbyte(8, 2, pcval)

#ifdef PD_COMPACT_MEM
/* Compact memory calls: the region math and handler fetch above cost
 * 14-18 bytes at EVERY site (~18.5k sites = the biggest slice of the
 * emitted stream, see NOTES.md audit). Shrink each site to
 * movw/movt(pc)+BL into a shared stub dispatcher that does the same
 * math once, hot in I-cache. Same ABI: handler returns to the site. */
void t2_mem_dispatch_0(void); void t2_mem_dispatch_1(void);
void t2_mem_dispatch_2(void); void t2_mem_dispatch_3(void);
void t2_mem_dispatch_4(void); void t2_mem_dispatch_5(void);
void t2_mem_dispatch_6(void); void t2_mem_dispatch_7(void);
void t2_mem_dispatch_8(void);

static void (*const t2_mem_dispatch_ptrs[9])(void) = {
  t2_mem_dispatch_0, t2_mem_dispatch_1, t2_mem_dispatch_2,
  t2_mem_dispatch_3, t2_mem_dispatch_4, t2_mem_dispatch_5,
  t2_mem_dispatch_6, t2_mem_dispatch_7, t2_mem_dispatch_8,
};

#define generate_mem_call_compact(tblnum, pcval)                              \
  t2_load_imm32(reg_gpc, pcval);                                              \
  t2_bl(translation_ptr, t2_mem_dispatch_ptrs[tblnum])                        \

#undef generate_store_call
#define generate_store_call(tblnum, pcval)                                    \
  generate_mem_call_compact(tblnum, pcval)
#undef generate_load_call_byte
#define generate_load_call_byte(tblnum, pcval)                                \
  generate_mem_call_compact(tblnum, pcval)
#undef generate_load_call_mbyte
#define generate_load_call_mbyte(tblnum, abits, pcval)                        \
  generate_mem_call_compact(tblnum, pcval)
#endif /* PD_COMPACT_MEM */

#ifdef PD_INLINE_MEM
/* Inline load fastpath: memory_map_read covers BIOS/EWRAM/IWRAM/IO/VRAM
 * (with mirrors) and resident ROM pages, NULL elsewhere - so a page lookup
 * plus direct load serves the hot regions with no handler call, falling
 * back to the dispatch tables for palette/OAM/unmapped/evicted pages.
 * Accuracy note: region-0 loads bypass the open-bus PC check (accepted).
 * Flag-transparent: no S-forms, CBZ does not read or write flags. */

#define t2_inline_ld_head(fast_size)                                          \
  t2_mov_reg_shift(0, reg_a2, reg_a0, T2SHIFT_LSR, 15);                       \
  t2_addw(reg_a2, reg_a2, (0xD00 >> 2));    /* RDMAP_OFF in words */          \
  t2_ldr_reg(reg_a2, reg_base, reg_a2, 2);                                    \
  t2_cbz(reg_a2, (fast_size) + 2)           /* page miss -> fallback */       \

#define t2_inline_ld_tail(tblnum, abits, pcval)                               \
{                                                                             \
  u8 *t2_bw_site = translation_ptr;                                          \
  t2_b_w(translation_ptr, translation_ptr);  /* over the fallback */          \
  t2_load_imm32(reg_gpc, pcval);                                              \
  mem_calc_region(abits);                                                     \
  t2_addw(reg_a2, reg_a2, (STORE_TBL_OFF + 68*tblnum + 4) >> 2);              \
  t2_ldr_reg(reg_a2, reg_base, reg_a2, 2);                                    \
  t2_blx(reg_a2);                                                             \
  t2_patch_branch(t2_bw_site, translation_ptr);                               \
}                                                                             \

#undef generate_load_call_u8
#undef generate_load_call_s8
#undef generate_load_call_u16
#undef generate_load_call_s16
#undef generate_load_call_u32

#define generate_load_call_u8(pcval)                                          \
  t2_inline_ld_head(8);                                                       \
  t2_ubfx(reg_a1, reg_a0, 0, 15);                                             \
  t2_ldrb_reg(reg_rv, reg_a2, reg_a1, 0);                                     \
  t2_inline_ld_tail(4, 0, pcval)                                              \

#define generate_load_call_s8(pcval)                                          \
  t2_inline_ld_head(8);                                                       \
  t2_ubfx(reg_a1, reg_a0, 0, 15);                                             \
  t2_ldrsb_reg(reg_rv, reg_a2, reg_a1, 0);                                    \
  t2_inline_ld_tail(5, 0, pcval)                                              \

#define generate_load_call_u16(pcval)                                         \
  t2_inline_ld_head(12);                                                      \
  t2_ubfx(reg_a1, reg_a0, 0, 15);                                             \
  t2_dp_imm(T2OP_BIC, 0, reg_a1, reg_a1, 1);                                  \
  t2_ldrh_reg(reg_rv, reg_a2, reg_a1, 0);                                     \
  t2_inline_ld_tail(6, 1, pcval)                                              \

#define generate_load_call_s16(pcval)                                         \
  t2_inline_ld_head(12);                                                      \
  t2_ubfx(reg_a1, reg_a0, 0, 15);                                             \
  t2_dp_imm(T2OP_BIC, 0, reg_a1, reg_a1, 1);                                  \
  t2_ldrsh_reg(reg_rv, reg_a2, reg_a1, 0);                                    \
  t2_inline_ld_tail(7, 1, pcval)                                              \

#define generate_load_call_u32(pcval)                                         \
  t2_inline_ld_head(12);                                                      \
  t2_ubfx(reg_a1, reg_a0, 0, 15);                                             \
  t2_dp_imm(T2OP_BIC, 0, reg_a1, reg_a1, 3);                                  \
  t2_ldr_reg(reg_rv, reg_a2, reg_a1, 0);                                      \
  t2_inline_ld_tail(8, 2, pcval)                                              \

#endif /* PD_INLINE_MEM */

#define arm_access_memory_load(mem_type)                                      \
  cycle_count += 2;                                                           \
  generate_load_call_##mem_type(pc);                                          \
  arm_generate_store_reg_pc_no_flags(reg_rv, rd)                              \

#define arm_access_memory_store(mem_type)                                     \
  cycle_count++;                                                              \
  arm_generate_load_reg_pc(reg_a1, rd, 12);                                   \
  generate_store_call_##mem_type((pc + 4))                                    \

/* Calculate the address into a0 from _rn, _rm */

#define arm_access_memory_adjust_reg_sh_up(ireg)                              \
  t2_dp_reg(T2OP_ADD, 0, ireg, _rn, _rm, ((opcode >> 5) & 0x03),              \
   ((opcode >> 7) & 0x1F))                                                    \

#define arm_access_memory_adjust_reg_sh_down(ireg)                            \
  t2_dp_reg(T2OP_SUB, 0, ireg, _rn, _rm, ((opcode >> 5) & 0x03),              \
   ((opcode >> 7) & 0x1F))                                                    \

#define arm_access_memory_adjust_reg_up(ireg)                                 \
  t2_dp_reg(T2OP_ADD, 0, ireg, _rn, _rm, T2SHIFT_LSL, 0)                      \

#define arm_access_memory_adjust_reg_down(ireg)                               \
  t2_dp_reg(T2OP_SUB, 0, ireg, _rn, _rm, T2SHIFT_LSL, 0)                      \

/* Guest offsets are at most 12 bits: ADDW/SUBW take them in one shot. */
#define arm_access_memory_adjust_imm_up(ireg)                                 \
  t2_addw(ireg, _rn, offset)                                                  \

#define arm_access_memory_adjust_imm_down(ireg)                               \
  t2_subw(ireg, _rn, offset)                                                  \

#define arm_access_memory_pre(type, direction)                                \
  arm_access_memory_adjust_##type##_##direction(reg_a0)                       \

#define arm_access_memory_pre_wb(type, direction)                             \
  arm_access_memory_adjust_##type##_##direction(reg_a0);                      \
  arm_generate_store_reg(reg_a0, rn)                                          \

#define arm_access_memory_post(type, direction)                               \
  u32 _rn_dest = arm_prepare_store_reg(reg_a1, rn);                           \
  if(_rn != reg_a0)                                                           \
  {                                                                           \
    arm_generate_load_reg(reg_a0, rn);                                        \
  }                                                                           \
  arm_access_memory_adjust_##type##_##direction(_rn_dest);                    \
  arm_complete_store_reg(_rn_dest, rn)                                        \

#define arm_data_trans_reg(adjust_op, direction)                              \
  arm_decode_data_trans_reg();                                                \
  u32 _rn = arm_prepare_load_reg_pc(&translation_ptr, reg_a0, rn, pc + 8);    \
  u32 _rm = arm_prepare_load_reg(&translation_ptr, reg_a1, rm);               \
  arm_access_memory_##adjust_op(reg_sh, direction)                            \

#define arm_data_trans_imm(adjust_op, direction)                              \
  arm_decode_data_trans_imm();                                                \
  u32 _rn = arm_prepare_load_reg_pc(&translation_ptr, reg_a0, rn, pc + 8);    \
  arm_access_memory_##adjust_op(imm, direction)                               \

#define arm_data_trans_half_reg(adjust_op, direction)                         \
  arm_decode_half_trans_r();                                                  \
  u32 _rn = arm_prepare_load_reg_pc(&translation_ptr, reg_a0, rn, pc + 8);    \
  u32 _rm = arm_prepare_load_reg(&translation_ptr, reg_a1, rm);               \
  arm_access_memory_##adjust_op(reg, direction)                               \

#define arm_data_trans_half_imm(adjust_op, direction)                         \
  arm_decode_half_trans_of();                                                 \
  u32 _rn = arm_prepare_load_reg_pc(&translation_ptr, reg_a0, rn, pc + 8);    \
  arm_access_memory_##adjust_op(imm, direction)                               \

#define arm_access_memory(access_type, direction, adjust_op, mem_type,        \
 offset_type)                                                                 \
{                                                                             \
  arm_data_trans_##offset_type(adjust_op, direction);                         \
  arm_access_memory_##access_type(mem_type);                                  \
}                                                                             \

#define word_bit_count(word)                                                  \
  (bit_count[word >> 8] + bit_count[word & 0xFF])                             \

/* TODO: Make these use cached registers. Implement iwram_stack_optimize. */

#define arm_block_memory_load()                                               \
  generate_load_call_u32((pc + 8));                                           \
  arm_generate_store_reg(reg_rv, i)                                           \

#define arm_block_memory_store()                                              \
  arm_generate_load_reg_pc(reg_a1, i, 8);                                     \
  generate_store_call_u32_safe(0)                                             \

#define arm_block_memory_final_load(writeback_type)                           \
  arm_block_memory_load()                                                     \

#define arm_block_memory_final_store(writeback_type)                          \
  arm_generate_load_reg_pc(reg_a1, i, 12);                                    \
  arm_block_memory_writeback_post_store(writeback_type);                      \
  generate_store_call_u32((pc + 4))                                           \

#define arm_block_memory_adjust_pc_store()                                    \

#define arm_block_memory_adjust_pc_load()                                     \
  if(reg_list & 0x8000)                                                       \
  {                                                                           \
    generate_indirect_branch_arm();                                           \
  }                                                                           \

#define arm_block_memory_offset_down_a()                                      \
  generate_sub_imm(reg_s0, ((word_bit_count(reg_list) * 4) - 4), 0)           \

#define arm_block_memory_offset_down_b()                                      \
  generate_sub_imm(reg_s0, (word_bit_count(reg_list) * 4), 0)                 \

#define arm_block_memory_offset_no()                                          \

#define arm_block_memory_offset_up()                                          \
  generate_add_imm(reg_s0, 4, 0)                                              \

#define arm_block_memory_writeback_down()                                     \
  arm_generate_load_reg(reg_a2, rn);                                          \
  generate_sub_imm(reg_a2, (word_bit_count(reg_list) * 4), 0);                \
  arm_generate_store_reg(reg_a2, rn)                                          \

#define arm_block_memory_writeback_up()                                       \
  arm_generate_load_reg(reg_a2, rn);                                          \
  generate_add_imm(reg_a2, (word_bit_count(reg_list) * 4), 0);                \
  arm_generate_store_reg(reg_a2, rn)                                          \

#define arm_block_memory_writeback_no()

/* Only emit writeback if the register is not in the list */

#define arm_block_memory_writeback_pre_load(writeback_type)                   \
  if(!((reg_list >> rn) & 0x01))                                              \
  {                                                                           \
    arm_block_memory_writeback_##writeback_type();                            \
  }                                                                           \

#define arm_block_memory_writeback_post_store(writeback_type)                 \
  arm_block_memory_writeback_##writeback_type()                               \

#define arm_block_memory_writeback_pre_store(writeback_type)

#define arm_block_memory(access_type, offset_type, writeback_type, s_bit)     \
{                                                                             \
  arm_decode_block_trans();                                                   \
  u32 offset = 0;                                                             \
  u32 i;                                                                      \
                                                                              \
  arm_generate_load_reg(reg_s0, rn);                                          \
  arm_block_memory_offset_##offset_type();                                    \
  arm_block_memory_writeback_pre_##access_type(writeback_type);               \
  t2_dp_imm_auto(T2OP_BIC, 0, reg_s0, reg_s0, 0x03);                          \
  arm_generate_store_reg(reg_s0, REG_SAVE);                                   \
                                                                              \
  for(i = 0; i < 16; i++)                                                     \
  {                                                                           \
    if((reg_list >> i) & 0x01)                                                \
    {                                                                         \
      cycle_count++;                                                          \
      arm_generate_load_reg(reg_s0, REG_SAVE);                                \
      generate_add_reg_reg_imm(reg_a0, reg_s0, offset, 0);                    \
      if(reg_list & ~((2 << i) - 1))                                          \
      {                                                                       \
        arm_block_memory_##access_type();                                     \
        offset += 4;                                                          \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        arm_block_memory_final_##access_type(writeback_type);                 \
        break;                                                                \
      }                                                                       \
    }                                                                         \
  }                                                                           \
                                                                              \
  arm_block_memory_adjust_pc_##access_type();                                 \
}                                                                             \

#define arm_swap(type)                                                        \
{                                                                             \
  arm_decode_swap();                                                          \
  cycle_count += 3;                                                           \
  arm_generate_load_reg(reg_a0, rn);                                          \
  generate_load_call_##type((pc + 8));                                        \
  generate_mov(reg_a2, reg_rv);                                               \
  arm_generate_load_reg(reg_a0, rn);                                          \
  arm_generate_load_reg(reg_a1, rm);                                          \
  arm_generate_store_reg(reg_a2, rd);                                         \
  generate_store_call_##type((pc + 4));                                       \
}                                                                             \

/* --- Thumb guest translation ------------------------------------------------ */

#define thumb_generate_op_reg(name, _rd, _rs, _rn)                            \
  u32 __rm = thumb_prepare_load_reg(&translation_ptr, reg_rm, _rn);           \
  generate_op_##name##_reg_immshift(__rd, __rn, __rm, T2SHIFT_LSL, 0)         \

#define thumb_generate_op_imm(name, _rd, _rs, imm_)                           \
{                                                                             \
  u32 imm_ror = 0;                                                            \
  generate_op_##name##_imm(__rd, __rn);                                       \
}                                                                             \

#define thumb_data_proc(type, name, op_type, _rd, _rs, _rn)                   \
{                                                                             \
  thumb_decode_##type();                                                      \
  u32 __rn = thumb_prepare_load_reg(&translation_ptr, reg_rn, _rs);           \
  u32 __rd = thumb_prepare_store_reg(reg_rd, _rd);                            \
  thumb_generate_op_##op_type(name, _rd, _rs, _rn);                           \
  thumb_complete_store_reg(__rd, _rd);                                        \
}                                                                             \

#define thumb_data_proc_test(type, name, op_type, _rd, _rs)                   \
{                                                                             \
  thumb_decode_##type();                                                      \
  u32 __rn = thumb_prepare_load_reg(&translation_ptr, reg_rn, _rd);           \
  thumb_generate_op_##op_type(name, 0, _rd, _rs);                             \
}                                                                             \

#define thumb_data_proc_unary(type, name, op_type, _rd, _rs)                  \
{                                                                             \
  thumb_decode_##type();                                                      \
  u32 __rd = thumb_prepare_store_reg(reg_rd, _rd);                            \
  thumb_generate_op_##op_type(name, _rd, 0, _rs);                             \
  thumb_complete_store_reg(__rd, _rd);                                        \
}                                                                             \

#define complete_store_reg_pc_thumb()                                         \
  if(rd == 15)                                                                \
  {                                                                           \
    generate_indirect_branch_cycle_update(thumb);                             \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    thumb_complete_store_reg(_rd, rd);                                        \
  }                                                                           \

#define thumb_data_proc_hi(name)                                              \
{                                                                             \
  thumb_decode_hireg_op();                                                    \
  u32 _rd = thumb_prepare_load_reg_pc(&translation_ptr, reg_rd, rd, pc + 4);  \
  u32 _rs = thumb_prepare_load_reg_pc(&translation_ptr, reg_rn, rs, pc + 4);  \
  generate_op_##name##_reg_immshift(_rd, _rd, _rs, T2SHIFT_LSL, 0);           \
  complete_store_reg_pc_thumb();                                              \
}                                                                             \

#define thumb_data_proc_test_hi(name)                                         \
{                                                                             \
  thumb_decode_hireg_op();                                                    \
  u32 _rd = thumb_prepare_load_reg_pc(&translation_ptr, reg_rd, rd, pc + 4);  \
  u32 _rs = thumb_prepare_load_reg_pc(&translation_ptr, reg_rn, rs, pc + 4);  \
  generate_op_##name##_reg_immshift(0, _rd, _rs, T2SHIFT_LSL, 0);             \
}                                                                             \

#define thumb_data_proc_mov_hi()                                              \
{                                                                             \
  thumb_decode_hireg_op();                                                    \
  u32 _rs = thumb_prepare_load_reg_pc(&translation_ptr, reg_rn, rs, pc + 4);  \
  u32 _rd = thumb_prepare_store_reg(reg_rd, rd);                              \
  generate_mov(_rd, _rs);                                                     \
  complete_store_reg_pc_thumb();                                              \
}                                                                             \

#define thumb_load_pc(_rd)                                                    \
{                                                                             \
  thumb_decode_imm();                                                         \
  u32 __rd = thumb_prepare_store_reg(reg_rd, _rd);                            \
  generate_load_pc(__rd, (((pc & ~2) + 4) + (imm * 4)));                      \
  thumb_complete_store_reg(__rd, _rd);                                        \
}                                                                             \

#define thumb_load_sp(_rd)                                                    \
{                                                                             \
  thumb_decode_imm();                                                         \
  u32 __sp = thumb_prepare_load_reg(&translation_ptr, reg_a0, REG_SP);        \
  u32 __rd = thumb_prepare_store_reg(reg_a0, _rd);                            \
  t2_addw(__rd, __sp, (imm * 4));                                             \
  thumb_complete_store_reg(__rd, _rd);                                        \
}                                                                             \

#define thumb_adjust_sp_up()                                                  \
  t2_addw(_sp, _sp, (imm * 4))                                                \

#define thumb_adjust_sp_down()                                                \
  t2_subw(_sp, _sp, (imm * 4))                                                \

#define thumb_adjust_sp(direction)                                            \
{                                                                             \
  thumb_decode_add_sp();                                                      \
  u32 _sp = thumb_prepare_load_reg(&translation_ptr, reg_a0, REG_SP);         \
  thumb_adjust_sp_##direction();                                              \
  thumb_complete_store_reg(_sp, REG_SP);                                      \
}                                                                             \

#define generate_op_lsl_reg(_rd, _rm, _rs)                                    \
  generate_op_movs_reg_regshift(_rd, 0, _rm, T2SHIFT_LSL, _rs)                \

#define generate_op_lsr_reg(_rd, _rm, _rs)                                    \
  generate_op_movs_reg_regshift(_rd, 0, _rm, T2SHIFT_LSR, _rs)                \

#define generate_op_asr_reg(_rd, _rm, _rs)                                    \
  generate_op_movs_reg_regshift(_rd, 0, _rm, T2SHIFT_ASR, _rs)                \

#define generate_op_ror_reg(_rd, _rm, _rs)                                    \
  generate_op_movs_reg_regshift(_rd, 0, _rm, T2SHIFT_ROR, _rs)                \

#define generate_op_lsl_imm(_rd, _rm)                                         \
  generate_op_movs_reg_immshift(_rd, 0, _rm, T2SHIFT_LSL, imm)                \

#define generate_op_lsr_imm(_rd, _rm)                                         \
  generate_op_movs_reg_immshift(_rd, 0, _rm, T2SHIFT_LSR, imm)                \

#define generate_op_asr_imm(_rd, _rm)                                         \
  generate_op_movs_reg_immshift(_rd, 0, _rm, T2SHIFT_ASR, imm)                \

#define generate_op_ror_imm(_rd, _rm)                                         \
  generate_op_movs_reg_immshift(_rd, 0, _rm, T2SHIFT_ROR, imm)                \

#define thumb_generate_shift_reg(op_type)                                     \
  u32 __rm = thumb_prepare_load_reg(&translation_ptr, reg_rd, rd);            \
  u32 __rs = thumb_prepare_load_reg(&translation_ptr, reg_rs, rs);            \
  generate_op_##op_type##_reg(__rd, __rm, __rs)                               \

#define thumb_generate_shift_imm(op_type)                                     \
  u32 __rs = thumb_prepare_load_reg(&translation_ptr, reg_rs, rs);            \
  generate_op_##op_type##_imm(__rd, __rs)                                     \

#define thumb_shift(decode_type, op_type, value_type)                         \
{                                                                             \
  thumb_decode_##decode_type();                                               \
  u32 __rd = thumb_prepare_store_reg(reg_rd, rd);                             \
  thumb_generate_shift_##value_type(op_type);                                 \
  thumb_complete_store_reg(__rd, rd);                                         \
}                                                                             \

/* Operation types: imm, mem_reg, mem_imm */

#define thumb_load_pc_pool_const(reg_rd, value)                               \
  u32 rgdst = thumb_prepare_store_reg(reg_a0, reg_rd);                        \
  generate_load_pc(rgdst, (value));                                           \
  thumb_complete_store_reg(rgdst, reg_rd)

#define thumb_access_memory_load(mem_type, _rd)                               \
  cycle_count += 2;                                                           \
  generate_load_call_##mem_type(pc);                                          \
  thumb_generate_store_reg(reg_rv, _rd)                                       \

#define thumb_access_memory_store(mem_type, _rd)                              \
  cycle_count++;                                                              \
  thumb_generate_load_reg(reg_a1, _rd);                                       \
  generate_store_call_##mem_type((pc + 2))                                    \

#define thumb_access_memory_generate_address_pc_relative(offset, _rb, _ro)    \
  generate_load_pc(reg_a0, (offset))                                          \

#define thumb_access_memory_generate_address_reg_imm(offset, _rb, _ro)        \
  u32 __rb = thumb_prepare_load_reg(&translation_ptr, reg_a0, _rb);           \
  t2_addw(reg_a0, __rb, offset)                                               \

#define thumb_access_memory_generate_address_reg_imm_sp(offset, _rb, _ro)     \
  u32 __rb = thumb_prepare_load_reg(&translation_ptr, reg_a0, _rb);           \
  t2_addw(reg_a0, __rb, (offset * 4))                                         \

#define thumb_access_memory_generate_address_reg_reg(offset, _rb, _ro)        \
  u32 __rb = thumb_prepare_load_reg(&translation_ptr, reg_a0, _rb);           \
  u32 __ro = thumb_prepare_load_reg(&translation_ptr, reg_a1, _ro);           \
  t2_dp_reg(T2OP_ADD, 0, reg_a0, __rb, __ro, T2SHIFT_LSL, 0)                  \

#define thumb_access_memory(access_type, op_type, _rd, _rb, _ro,              \
 address_type, offset, mem_type)                                              \
{                                                                             \
  thumb_decode_##op_type();                                                   \
  thumb_access_memory_generate_address_##address_type(offset, _rb, _ro);      \
  thumb_access_memory_##access_type(mem_type, _rd);                           \
}                                                                             \

#define thumb_block_address_preadjust_down()                                  \
  generate_sub_imm(reg_s0, (bit_count[reg_list] * 4), 0)                      \

#define thumb_block_address_preadjust_push_lr()                               \
  generate_sub_imm(reg_s0, ((bit_count[reg_list] + 1) * 4), 0)                \

#define thumb_block_address_preadjust_no()                                    \

#define thumb_block_address_postadjust_no(base_reg)                           \
  thumb_generate_store_reg(reg_s0, base_reg)                                  \

#define thumb_block_address_postadjust_up(base_reg)                           \
  generate_add_reg_reg_imm(reg_a0, reg_s0, (bit_count[reg_list] * 4), 0);     \
  thumb_generate_store_reg(reg_a0, base_reg)                                  \

#define thumb_block_address_postadjust_pop_pc(base_reg)                       \
  generate_add_reg_reg_imm(reg_a0, reg_s0,                                    \
   ((bit_count[reg_list] + 1) * 4), 0);                                       \
  thumb_generate_store_reg(reg_a0, base_reg)                                  \

#define thumb_block_address_postadjust_push_lr(base_reg)                      \
  thumb_generate_store_reg(reg_s0, base_reg)                                  \

#define thumb_block_memory_extra_no()                                         \

#define thumb_block_memory_extra_up()                                         \

#define thumb_block_memory_extra_down()                                       \

#define thumb_block_memory_extra_pop_pc()                                     \
  thumb_generate_load_reg(reg_s0, REG_SAVE);                                  \
  generate_add_reg_reg_imm(reg_a0, reg_s0, (bit_count[reg_list] * 4), 0);     \
  generate_load_call_u32((pc + 4));                                           \
  generate_indirect_branch_cycle_update(thumb)                                \

#define thumb_block_memory_extra_push_lr(base_reg)                            \
  thumb_generate_load_reg(reg_s0, REG_SAVE);                                  \
  generate_add_reg_reg_imm(reg_a0, reg_s0, (bit_count[reg_list] * 4), 0);     \
  thumb_generate_load_reg(reg_a1, REG_LR);                                    \
  generate_store_call_u32_safe(0)

#define thumb_block_memory_load()                                             \
  generate_load_call_u32((pc + 4));                                           \
  thumb_generate_store_reg(reg_rv, i)                                         \

#define thumb_block_memory_store()                                            \
  thumb_generate_load_reg(reg_a1, i);                                         \
  generate_store_call_u32_safe(0)

#define thumb_block_memory_final_load()                                       \
  thumb_block_memory_load()                                                   \

#define thumb_block_memory_final_store()                                      \
  thumb_generate_load_reg(reg_a1, i);                                         \
  generate_store_call_u32((pc + 2))                                           \

#define thumb_block_memory_final_no(access_type)                              \
  thumb_block_memory_final_##access_type()                                    \

#define thumb_block_memory_final_up(access_type)                              \
  thumb_block_memory_final_##access_type()                                    \

#define thumb_block_memory_final_down(access_type)                            \
  thumb_block_memory_final_##access_type()                                    \

#define thumb_block_memory_final_push_lr(access_type)                         \
  thumb_block_memory_##access_type()                                          \

#define thumb_block_memory_final_pop_pc(access_type)                          \
  thumb_block_memory_##access_type()                                          \

#define thumb_block_memory(access_type, pre_op, post_op, base_reg)            \
{                                                                             \
  thumb_decode_rlist();                                                       \
  u32 i;                                                                      \
  u32 offset = 0;                                                             \
                                                                              \
  thumb_generate_load_reg(reg_s0, base_reg);                                  \
  t2_dp_imm_auto(T2OP_BIC, 0, reg_s0, reg_s0, 0x03);                          \
  thumb_block_address_preadjust_##pre_op();                                   \
  thumb_block_address_postadjust_##post_op(base_reg);                         \
  thumb_generate_store_reg(reg_s0, REG_SAVE);                                 \
                                                                              \
  for(i = 0; i < 8; i++)                                                      \
  {                                                                           \
    if((reg_list >> i) & 0x01)                                                \
    {                                                                         \
      cycle_count++;                                                          \
      thumb_generate_load_reg(reg_s0, REG_SAVE);                              \
      generate_add_reg_reg_imm(reg_a0, reg_s0, offset, 0);                    \
      if(reg_list & ~((2 << i) - 1))                                          \
      {                                                                       \
        thumb_block_memory_##access_type();                                   \
        offset += 4;                                                          \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        thumb_block_memory_final_##post_op(access_type);                      \
        break;                                                                \
      }                                                                       \
    }                                                                         \
  }                                                                           \
                                                                              \
  thumb_block_memory_extra_##post_op();                                       \
}                                                                             \

/* --- Branches ---------------------------------------------------------------- */

#define thumb_conditional_branch(condition)                                   \
{                                                                             \
  generate_cycle_update();                                                    \
  generate_branch_filler(condition_opposite_##condition, backpatch_address);  \
  generate_branch_no_cycle_update(                                            \
   block_exits[block_exit_position].branch_source,                            \
   block_exits[block_exit_position].branch_target, thumb);                    \
  generate_branch_patch_conditional(backpatch_address, translation_ptr);      \
  block_exit_position++;                                                      \
}                                                                             \

#define arm_conditional_block_header()                                        \
  generate_cycle_update();                                                    \
  /* This will choose the opposite condition */                               \
  condition ^= 0x01;                                                          \
  generate_branch_filler(condition, backpatch_address)                        \

#define arm_b()                                                               \
  generate_branch(arm)                                                        \

#define arm_bl()                                                              \
  generate_update_pc((pc + 4));                                               \
  arm_generate_store_reg(reg_a0, REG_LR);                                     \
  generate_branch(arm)                                                        \

#define arm_bx()                                                              \
  arm_decode_branchx(opcode);                                                 \
  arm_generate_load_reg_pc(reg_a0, rn, 8);                                    \
  generate_indirect_branch_dual();                                            \

#define arm_swi()                                                             \
  t2_load_imm32(reg_gpc, (pc + 4));                                           \
  generate_function_far_call(armfn_swi_arm);                                  \
  generate_branch(arm)                                                        \

#define thumb_b()                                                             \
  generate_branch(thumb)                                                      \

#define thumb_bl()                                                            \
  generate_update_pc(((pc + 2) | 0x01));                                      \
  thumb_generate_store_reg(reg_a0, REG_LR);                                   \
  generate_branch(thumb)                                                      \

#define thumb_blh()                                                           \
{                                                                             \
  thumb_decode_branch();                                                      \
  u32 offlo = (offset * 2) & 0xFF;                                            \
  u32 offhi = (offset * 2) >> 8;                                              \
  generate_update_pc(((pc + 2) | 0x01));                                      \
  thumb_generate_load_reg(reg_a1, REG_LR);                                    \
  thumb_generate_store_reg(reg_a0, REG_LR);                                   \
  generate_add_reg_reg_imm(reg_a0, reg_a1, offlo, 0);                         \
  if (offhi) {                                                                \
    t2_dp_imm_auto(T2OP_ADD, 0, reg_a0, reg_a0, offhi << 8);                  \
  }                                                                           \
  generate_indirect_branch_cycle_update(thumb);                               \
}                                                                             \

#define thumb_bx()                                                            \
{                                                                             \
  thumb_decode_hireg_op();                                                    \
  thumb_generate_load_reg_pc(reg_a0, rs, 4);                                  \
  generate_indirect_branch_cycle_update(dual_thumb);                          \
}                                                                             \

#define thumb_process_cheats()                                                \
  generate_function_far_call(armfn_cheat_thumb);

#define arm_process_cheats()                                                  \
  generate_function_far_call(armfn_cheat_arm);

#define thumb_swi()                                                           \
  t2_load_imm32(reg_gpc, (pc + 2));                                           \
  generate_function_far_call(armfn_swi_thumb);                                \
  /* We're in ARM mode now */                                                 \
  generate_branch(arm)                                                        \

/* SWI 6/7 division: hardware SDIV via the stub (see thumb2_stub.S). */
void *div6, *divarm7;
#define arm_hle_div(cpu_mode)                                                 \
  cycle_count += 11 + 32;                                                     \
  generate_function_call(div6);
#define arm_hle_div_arm(cpu_mode)                                             \
  cycle_count += 14 + 32;                                                     \
  generate_function_call(divarm7);

/* BIOS HLE (CpuSet/CpuFastSet/LZ77): native C instead of emulating the
 * BIOS loops. Stub bridges in thumb2_stub.S; C in pd_bios_hle.c. */
#ifdef PD_BIOS_HLE
void t2_hle_bios_arm(void);
void t2_hle_bios_thumb(void);
#define pd_bios_hle_handles(n)                                                \
  ((n) == 0x0B || (n) == 0x0C || (n) == 0x11 || (n) == 0x12)
#define arm_hle_bios(mode, num)                                               \
  t2_load_imm32(reg_gpc, num);                                                \
  generate_function_call(t2_hle_bios_##mode)
#else
#define pd_bios_hle_handles(n) 0
#define arm_hle_bios(mode, num)
#endif

#define generate_translation_gate(type)                                       \
  generate_update_pc(pc);                                                     \
  generate_indirect_branch_no_cycle_update(type)                              \

extern u32 st_handler_functions[4][17];
extern u32 ld_handler_functions[5][17];
extern u32 ld_swap_handler_functions[5][17];

// Tables used by the memory handlers (placed near reg_base)
extern u32 ld_lookup_tables[5][17];
extern u32 st_lookup_tables[4][17];

void init_emitter(bool must_swap) {
  // Generate handler table
  memcpy(st_lookup_tables, st_handler_functions, sizeof(st_lookup_tables));
  // Issue faster paths if swapping is not required
  if (must_swap)
    memcpy(ld_lookup_tables, ld_swap_handler_functions, sizeof(ld_lookup_tables));
  else
    memcpy(ld_lookup_tables, ld_handler_functions, sizeof(ld_lookup_tables));

  rom_cache_watermark = INITIAL_ROM_WATERMARK;

  // Divisions are served by the stub's hardware-SDIV helpers; no emitted
  // divider blob (Cortex-M7 has SDIV, faster than the unrolled version).
  div6 = (void *)t2_hle_div;
  divarm7 = (void *)t2_hle_divarm;

  // Now generate BIOS hooks
  init_bios_hooks();

  // Intialize function table
  reg[REG_USERDEF + armfn_gbaup_idle_arm]   = (u32)arm_update_gba_idle_arm;
  reg[REG_USERDEF + armfn_gbaup_idle_thumb] = (u32)arm_update_gba_idle_thumb;
  reg[REG_USERDEF + armfn_gbaup_arm]   = (u32)arm_update_gba_arm;
  reg[REG_USERDEF + armfn_gbaup_thumb] = (u32)arm_update_gba_thumb;
  reg[REG_USERDEF + armfn_swi_arm]   = (u32)execute_swi_arm;
  reg[REG_USERDEF + armfn_swi_thumb] = (u32)execute_swi_thumb;
  reg[REG_USERDEF + armfn_cheat_arm]   = (u32)arm_cheat_hook;
  reg[REG_USERDEF + armfn_cheat_thumb] = (u32)thumb_cheat_hook;
  reg[REG_USERDEF + armfn_store_cpsr]   = (u32)execute_store_cpsr;
  reg[REG_USERDEF + armfn_spsr_restore] = (u32)execute_spsr_restore;
  reg[REG_USERDEF + armfn_indirect_arm]   = (u32)arm_indirect_branch_arm;
  reg[REG_USERDEF + armfn_indirect_thumb] = (u32)arm_indirect_branch_thumb;
  reg[REG_USERDEF + armfn_indirect_dual_arm]   = (u32)arm_indirect_branch_dual_arm;
  reg[REG_USERDEF + armfn_indirect_dual_thumb] = (u32)arm_indirect_branch_dual_thumb;
  reg[REG_USERDEF + armfn_debug_trace] = (u32)trace_instruction;
}

u32 execute_arm_translate_internal(u32 cycles, void *regptr);
u32 execute_arm_translate(u32 cycles) {
  return execute_arm_translate_internal(cycles, &reg[0]);
}

#endif /* THUMB2_EMIT_H */
