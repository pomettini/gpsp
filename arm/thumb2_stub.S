@ gameplaySP - Thumb-2 (Cortex-M7) dynarec stub glue, ported from arm_stub.S
@ for the Playdate. Unified Thumb-2 syntax throughout.
@
@ ABI deviations from the ARM stub (see NOTES.md "M-profile deltas"):
@  * No CPSR on M-profile: flags save/restore uses APSR.
@  * No inline literals after BL: T32 cannot `add pc, lr, #4`. Instead the
@    emitter loads the guest PC into r10 (MOVW/MOVT) before calling any
@    handler that needs it, and every handler returns with plain `bx lr`.
@    execute_store_cpsr takes its masks in r1 (user) / r2 (privileged).
@  * All handler labels are .thumb_func so table entries get the Thumb bit
@    (an even address in a table would BLX to ARM state -> HardFault).
@  * Translation caches live in plain .bss: PSRAM is executable on the
@    Playdate (verified by pd_jit_smoke.c) and the SDK link script only
@    collects standard sections.

#include "../gpsp_config.h"

.syntax unified
.cpu cortex-m7
.thumb

#define defsymbl(symbol) \
.align 2;                \
.thumb_func;             \
.type symbol, %function ;\
.global symbol ;         \
.global _##symbol ;      \
symbol:                  \
_##symbol:

#define defsymbl_data(symbol) \
.align 2;                     \
.type symbol, %object ;       \
.global symbol ;              \
.global _##symbol ;           \
symbol:                       \
_##symbol:

@ Local handler labels still need the Thumb bit when their address is taken
@ into the dispatch tables below.
#define thumb_label(symbol) \
.align 1;                   \
.thumb_func;                \
.type symbol, %function ;   \
symbol:

.text
.align 2

#define REG_R0            (0 * 4)
#define REG_R1            (1 * 4)
#define REG_R2            (2 * 4)
#define REG_R3            (3 * 4)
#define REG_R4            (4 * 4)
#define REG_R5            (5 * 4)
#define REG_R6            (6 * 4)
#define REG_R7            (7 * 4)
#define REG_R8            (8 * 4)
#define REG_R9            (9 * 4)
#define REG_R10           (10 * 4)
#define REG_R11           (11 * 4)
#define REG_R12           (12 * 4)
#define REG_R13           (13 * 4)
#define REG_R14           (14 * 4)
#define REG_SP            (13 * 4)
#define REG_LR            (14 * 4)
#define REG_PC            (15 * 4)
#define REG_CPSR          (16 * 4)
#define CPU_MODE          (17 * 4)
#define CPU_HALT_STATE    (18 * 4)

#define REG_BUS_VALUE     (19 * 4)
#define REG_N_FLAG        (20 * 4)
#define REG_Z_FLAG        (21 * 4)
#define REG_C_FLAG        (22 * 4)
#define REG_V_FLAG        (23 * 4)
#define REG_SLEEP_CYCLES  (24 * 4)
#define OAM_UPDATED       (25 * 4)

#define CPU_ALERT_HALT    (1 << 0)
#define CPU_ALERT_SMC     (1 << 1)
#define CPU_ALERT_IRQ     (1 << 2)

#define reg_a0            r0
#define reg_a1            r1
#define reg_a2            r2

#define reg_base          r11
#define reg_flags         r9
#define reg_gpc           r10    /* guest PC carrier (Thumb-2 ABI) */

#define reg_cycles        r12

#define reg_x0            r3
#define reg_x1            r4
#define reg_x2            r5
#define reg_x3            r6
#define reg_x4            r7
#define reg_x5            r8

#define MODE_SUPERVISOR   0x13
#define SUPERVISOR_OFFSET 0x03

@ Memory offsets from reg_base to the different buffers
#define IWRAM_OFF      -0xA8000
#define VRAM_OFF       -0x98000
#define EWRAM_OFF      -0x80000
#define SPSR_RAM_OFF      0x100
#define STORE_TBL_OFF     0x118
#define REGMODE_RAM_OFF   0x400
#define OAM_RAM_OFF       0x500
#define PAL_RAM_OFF       0x900
#define RDMAP_OFF         0xD00
#define IOREG_OFF        0x8D00
#define PAL_CONV_OFF     0x9100

#define extract_u16(rd, rs) \
  uxth rd, rs

@ Will load the register set from memory into the appropriate cached registers.
@ See thumb2_emit.h for listing explanation.

#define load_registers_arm()                                                 ;\
  ldr reg_x0, [reg_base, #REG_R0]                                            ;\
  ldr reg_x1, [reg_base, #REG_R1]                                            ;\
  ldr reg_x2, [reg_base, #REG_R6]                                            ;\
  ldr reg_x3, [reg_base, #REG_R9]                                            ;\
  ldr reg_x4, [reg_base, #REG_R12]                                           ;\
  ldr reg_x5, [reg_base, #REG_R14]                                           ;\

#define load_registers_thumb()                                               ;\
  ldm reg_base, {reg_x0, reg_x1, reg_x2, reg_x3, reg_x4, reg_x5}

@ Will store the register set from cached registers back to memory.

#define store_registers_arm()                                                ;\
  str reg_x0, [reg_base, #REG_R0]                                            ;\
  str reg_x1, [reg_base, #REG_R1]                                            ;\
  str reg_x2, [reg_base, #REG_R6]                                            ;\
  str reg_x3, [reg_base, #REG_R9]                                            ;\
  str reg_x4, [reg_base, #REG_R12]                                           ;\
  str reg_x5, [reg_base, #REG_R14]                                           ;\

#define store_registers_thumb()                                              ;\
  stm reg_base, {reg_x0, reg_x1, reg_x2, reg_x3, reg_x4, reg_x5}

@ Z clear (ne) = Thumb mode: store Thumb layout, else ARM layout.

#define store_registers_cond()                                               ;\
  beq 81f                                                                    ;\
  stm reg_base, {reg_x0, reg_x1, reg_x2, reg_x3, reg_x4, reg_x5}             ;\
  b 80f                                                                      ;\
81:                                                                          ;\
  str reg_x0, [reg_base, #REG_R0]                                            ;\
  str reg_x1, [reg_base, #REG_R1]                                            ;\
  str reg_x2, [reg_base, #REG_R6]                                            ;\
  str reg_x3, [reg_base, #REG_R9]                                            ;\
  str reg_x4, [reg_base, #REG_R12]                                           ;\
  str reg_x5, [reg_base, #REG_R14]                                           ;\
80:

@ Returns an updated persistent cpsr with the cached flags register.
@ Uses reg as a temporary register and returns the CPSR here.

#define collapse_flags_no_update(reg)                                        ;\
  ldr reg, [reg_base, #REG_CPSR]          /* reg = cpsr                    */;\
  bic reg, reg, #0xF0000000               /* clear ALU flags in cpsr       */;\
  and reg_flags, reg_flags, #0xF0000000   /* clear non-ALU flags           */;\
  orr reg, reg, reg_flags                 /* update cpsr with ALU flags    */;\

@ Updates cpsr using the above macro.

#define collapse_flags(reg)                                                  ;\
  collapse_flags_no_update(reg)                                              ;\
  str reg, [reg_base, #REG_CPSR]                                             ;\

@ Loads the saved flags register from the persistent cpsr.

#define extract_flags()                                                      ;\
  ldr reg_flags, [reg_base, #REG_CPSR]                                       ;\
  msr apsr_nzcvq, reg_flags                                                  ;\

#define save_flags()                                                         ;\
  mrs reg_flags, apsr                                                        ;\

#define restore_flags()                                                      ;\
  msr apsr_nzcvq, reg_flags                                                  ;\

@ Align the stack to 64 bits (ABIs that don't require it, still recommend so)
#define call_c_saved_regs r2, r3, r12, lr

@ Calls a C function - saves all caller save registers which are important
@ to the dynarec.

#define call_c_function(function)                                            ;\
  push { call_c_saved_regs }                                                 ;\
  bl function                                                                ;\
  pop { call_c_saved_regs }                                                  ;\

@ Update the GBA hardware (video, sound, input, etc)

@ Input:
@ reg_gpc (r10): current guest PC (set by the emitted call site)

#define cycles_straight()  mvn r0, reg_cycles
#define cycles_add()       mov r0, #0

#define arm_update_gba_builder(name, mode, return_op)                        ;\
                                                                             ;\
defsymbl(arm_update_gba_##name)                                              ;\
  str reg_gpc, [reg_base, #REG_PC]        /* write out the PC              */;\
                                                                             ;\
  save_flags()                                                               ;\
  collapse_flags(r0)                      /* update the flags              */;\
                                                                             ;\
  store_registers_##mode()                /* save out registers            */;\
  cycles_##return_op()                    /* remaining cycles in arg0      */;\
  call_c_function(update_gba)             /* update GBA state              */;\
                                                                             ;\
  cmp r0, #0                              /* check MSB for frame completion*/;\
  blt return_to_main                                                         ;\
                                                                             ;\
  bic reg_cycles, r0, #0xF0000000         /* clear MSB, not part of count  */;\
  mvn reg_cycles, reg_cycles              /* we count negative to zero     */;\
                                                                             ;\
  tst r0, #0x40000000                     /* set if PC changed             */;\
  bne 1f                                  /* go jump/translate             */;\
                                                                             ;\
  load_registers_##mode()                 /* reload registers              */;\
  restore_flags()                                                            ;\
  bx lr                                   /* continue, no PC change        */;\
                                                                             ;\
1:                                                                           ;\
  ldr r1, [reg_base, #REG_CPSR]           /* r1 = flags                    */;\
  ldr r0, [reg_base, #REG_PC]             /* load new PC                   */;\
  tst r1, #0x20                           /* see if Thumb bit is set       */;\
  bne 2f                                  /* if so load Thumb PC           */;\
                                                                             ;\
  load_registers_arm()                    /* load ARM regs                 */;\
  call_c_function(block_lookup_address_arm)                                  ;\
  restore_flags()                                                            ;\
  orr r0, r0, #1                          /* Thumb bit for block entry   */;\
  bx r0                                   /* jump to new ARM block         */;\
2:                                                                           ;\
  load_registers_thumb()                  /* load Thumb regs               */;\
  call_c_function(block_lookup_address_thumb)                                ;\
  restore_flags()                                                            ;\
  orr r0, r0, #1                          /* Thumb bit for block entry   */;\
  bx r0                                   /* jump to new Thumb block       */;\
.size arm_update_gba_##name, .-arm_update_gba_##name

arm_update_gba_builder(arm, arm, straight)
arm_update_gba_builder(thumb, thumb, straight)

arm_update_gba_builder(idle_arm, arm, add)
arm_update_gba_builder(idle_thumb, thumb, add)

@ Cheat hooks for master function
@ This is called whenever PC == cheats-master-function
@ Just calls the C function to process cheats

#define cheat_hook_builder(mode)                                             ;\
defsymbl(mode##_cheat_hook)                                                  ;\
  save_flags()                                                               ;\
  store_registers_##mode()                                                   ;\
  call_c_function(process_cheats)                                            ;\
  load_registers_##mode()                                                    ;\
  restore_flags()                                                            ;\
  bx lr                                                                      ;\

cheat_hook_builder(arm)
cheat_hook_builder(thumb)

@ These are b stubs for performing indirect branches. They are not
@ linked to and don't return, instead they link elsewhere.

@ Input:
@ r0: PC to branch to

defsymbl(arm_indirect_branch_arm)
  save_flags()
  call_c_function(block_lookup_address_arm)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0

defsymbl(arm_indirect_branch_thumb)
  save_flags()
  call_c_function(block_lookup_address_thumb)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0

defsymbl(arm_indirect_branch_dual_arm)
  save_flags()
  tst r0, #0x01                           @ check lower bit
  bne 1f                                  @ if set going to Thumb mode
  add r0, #2                              @ two LSB are cleared after
  call_c_function(block_lookup_address_arm)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0                                   @ keep executing arm code
1:
  store_registers_arm()                   @ save out ARM registers
  ldr r1, [reg_base, #REG_CPSR]           @ load cpsr
  load_registers_thumb()                  @ load in Thumb registers
  orr r1, r1, #0x20                       @ set Thumb mode
  str r1, [reg_base, #REG_CPSR]           @ store flags
  call_c_function(block_lookup_address_thumb)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0
.size arm_indirect_branch_dual_arm, .-arm_indirect_branch_dual_arm

defsymbl(arm_indirect_branch_dual_thumb)
  save_flags()
  tst r0, #0x01                           @ check lower bit
  beq 1f                                  @ if clear going to ARM mode
  call_c_function(block_lookup_address_thumb)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0                                   @ keep executing thumb code
1:
  store_registers_thumb()                 @ save out Thumb registers
  ldr r1, [reg_base, #REG_CPSR]           @ load cpsr
  load_registers_arm()                    @ load in ARM registers
  bic r1, r1, #0x20                       @ clear Thumb mode
  str r1, [reg_base, #REG_CPSR]           @ store flags
  add r0, #2                              @ two LSB are cleared after
  call_c_function(block_lookup_address_arm)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0
.size arm_indirect_branch_dual_thumb, .-arm_indirect_branch_dual_thumb

@ Update the cpsr.

@ Input:
@ r0: new cpsr value
@ r1: bitmask (user mode)
@ r2: bitmask (privileged mode)
@ reg_gpc: current guest PC

defsymbl(execute_store_cpsr)
  save_flags()
  push { r3 }                             @ r3 is an allocated reg, keep it
  ldr r3, [reg_base, #CPU_MODE]           @ r3 = cpu_mode
  tst r3, #0x10                           @ privileged mode?
  it ne
  movne r1, r2                            @ r1 = mask for current privilege
  pop { r3 }

  and reg_flags, r0, r1                   @ reg_flags = new_cpsr & store_mask
  ldr r0, [reg_base, #REG_CPSR]           @ r0 = cpsr
  bic r0, r0, r1                          @ r0 = cpsr & ~store_mask
  orr reg_flags, reg_flags, r0            @ reg_flags = new_cpsr | cpsr

  mov r0, reg_flags                       @ also put new cpsr in r0

  store_registers_arm()                   @ save ARM registers
  mov r2, reg_gpc                         @ r2 = pc
  call_c_function(execute_store_cpsr_body)
  load_registers_arm()                    @ restore ARM registers

  cmp r0, #0                              @ check new PC
  bne 1f                                  @ if it's nonzero, translate

  restore_flags()
  bx lr                                   @ return

1:
  call_c_function(block_lookup_address_arm)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0                                   @ return to PC ARM address
.size execute_store_cpsr, .-execute_store_cpsr

@ Restore the cpsr from the mode spsr and mode shift.

@ Input:
@ r0: current pc

defsymbl(execute_spsr_restore)
  save_flags()
  ldr r2, [reg_base, #CPU_MODE]           @ r2 = cpu_mode
  ands r2, r2, #0xF                       @ Ignore privilege bits
  beq 2f                                  @ if user/system mode no side effects

  add r1, reg_base, #SPSR_RAM_OFF         @ r1 = spsr
  ldr r1, [r1, r2, lsl #2]                @ r1 = spsr[cpu_mode] (new cpsr)
  str r1, [reg_base, #REG_CPSR]           @ update cpsr
  mov reg_flags, r1                       @ also, update shadow flags

  @ This function call will pass r0 (address) and return it.
  store_registers_arm()                   @ save ARM registers
  call_c_function(execute_spsr_restore_body)

  ldr r1, [reg_base, #REG_CPSR]           @ r1 = cpsr
  tst r1, #0x20                           @ see if Thumb mode is set
  bne 1f                                  @ if so handle it

  load_registers_arm()                    @ restore ARM registers
2:
  call_c_function(block_lookup_address_arm)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0

1:
  load_registers_thumb()                  @ load Thumb registers
  call_c_function(block_lookup_address_thumb)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0

@ Setup the mode transition work for calling an SWI.

@ Input:
@ reg_gpc: current guest PC (next instruction after the SWI)

#define execute_swi_builder(mode)                                            ;\
                                                                             ;\
defsymbl(execute_swi_##mode)                                                 ;\
  save_flags()                                                               ;\
  add r1, reg_base, #REGMODE_RAM_OFF      /* r1 = reg_mode                 */;\
  /* REG_MODE(MODE_SUPERVISOR)[6] = pc                                     */;\
  str reg_gpc, [r1, #((SUPERVISOR_OFFSET * (7 * 4)) + (6 * 4))]              ;\
  collapse_flags_no_update(r0)            /* r0 = cpsr                     */;\
  add r1, reg_base, #SPSR_RAM_OFF         /* r1 = spsr                     */;\
  str r0, [r1, #(SUPERVISOR_OFFSET * 4)]  /* spsr[MODE_SUPERVISOR] = cpsr  */;\
  bic r0, r0, #0x3F                       /* clear mode flag in r0         */;\
  orr r0, r0, #(0x13 | 0x80)              /* supervisor mode + disable IRQ */;\
  str r0, [reg_base, #REG_CPSR]           /* update cpsr                   */;\
                                                                             ;\
  mov r0, #MODE_SUPERVISOR                                                   ;\
                                                                             ;\
  store_registers_##mode()                /* store regs for mode           */;\
  call_c_function(set_cpu_mode)           /* set the CPU mode to svsr      */;\
  load_registers_arm()                    /* load ARM regs                 */;\
  movw r0, #0x2004                        /* Update open BUS value         */;\
  movt r0, #0xe3a0                        /* (0xe3a02004)                  */;\
  str r0, [reg_base, #REG_BUS_VALUE]                                         ;\
                                                                             ;\
  restore_flags()                                                            ;\
  bx lr                                   /* return                        */;\

execute_swi_builder(arm)
execute_swi_builder(thumb)

@ Start program execution. Normally the mode should be Thumb and the
@ PC should be 0x8000000, however if a save state is preloaded this
@ will be different.

@ Input:
@ r0: initial value for cycle counter
@ r1: pointer to reg[] (reg_base)

defsymbl(execute_arm_translate_internal)

  @ save the registers to be able to return later
  push { r4, r5, r6, r7, r8, r9, r10, r11, r12, lr }

  mov reg_base, r1                        @ init base_reg

  mvn reg_cycles, r0                      @ load cycle counter

  @ Check whether the CPU is sleeping already, we should just wait for IRQs
  ldr r1, [reg_base, #CPU_HALT_STATE]
  cmp r1, #0
  bne cpu_sleep_loop
  b lookup_pc

@ Epilogue to return to the main thread (whatever called execute_arm_translate)

return_to_main:
  @ restore the saved regs and return
  pop { r4, r5, r6, r7, r8, r9, r10, r11, r12, lr }
  bx lr

@ Prepares stores for external write function (align + zero extend value)
#define store_align_8()   and r1, r1, #0xff
#define store_align_16()  bic r0, r0, #0x01; extract_u16(r1, r1)
#define store_align_32()  bic r0, r0, #0x03

@ T32 has no LSL+LSR addressing tricks; UBFX isolates the mirror bits.
#define mask_addr_8(nbits)                                                   ;\
  ubfx r0, r0, #0, #(nbits)                  /* keep low nbits               */;\

#define mask_addr_16(nbits)                                                  ;\
  ubfx r0, r0, #0, #(nbits)                  /* keep low nbits               */;\
  bic r0, r0, #1                           /* LSB is also zero             */;\

#define mask_addr_32(nbits)                                                  ;\
  ubfx r0, r0, #0, #(nbits)                  /* keep low nbits               */;\
  bic r0, r0, #3                           /* 2 LSB are also zero          */;\

@ Vram, OAM and palette memories can only be accessed at a 16 bit boundary
#define mask_addr_bus16_32(nbits) mask_addr_32(nbits)
#define mask_addr_bus16_16(nbits) mask_addr_16(nbits)
#define mask_addr_bus16_8(nbits)  mask_addr_16(nbits)

#define dup8(reg)  bic r1, r1, #0xff00; orr r1, r1, r1, lsl #8;
#define dup16(reg)
#define dup32(reg)

@ Write out to memory.

@ Input:
@ r0: address
@ r1: value
@ reg_gpc: current guest PC (for SMC handling)

#define execute_store_builder(store_type, str_op, str_op16, load_op)         ;\
                                                                             ;\
thumb_label(ext_store_u##store_type)                                         ;\
  save_flags()                                                               ;\
  str reg_gpc, [reg_base, #REG_PC]        /* write out PC                  */;\
  store_align_##store_type()                                                 ;\
  call_c_function(write_memory##store_type)                                  ;\
  restore_flags()                                                            ;\
  bx lr                                   /* return                        */;\
                                                                             ;\
thumb_label(ext_io_store_u##store_type)                                      ;\
  save_flags()                                                               ;\
  str reg_gpc, [reg_base, #REG_PC]        /* write out PC                  */;\
                                                                             ;\
  call_c_function(write_io_register##store_type)                             ;\
                                                                             ;\
  cmp r0, #0                                                                 ;\
  bne write_epilogue                      /* handle additional write stuff */;\
  restore_flags()                                                            ;\
  bx lr                                   /* return                        */;\
                                                                             ;\
thumb_label(ext_store_iwram_u##store_type)                                   ;\
  save_flags()                                                               ;\
  mask_addr_##store_type(15)              /* Mask to mirror memory (+align)*/;\
  add r2, reg_base, #(IWRAM_OFF+0x8000)   /* r2 = iwram base               */;\
  str_op r1, [r0, r2]                     /* store data                    */;\
  sub r2, r2, #0x8000                     /* r2 = iwram smc base           */;\
  load_op r1, [r0, r2]                    /* r1 = SMC sentinel             */;\
  cmp r1, #0                              /* Check value, should be zero   */;\
  bne 3f                                  /* if so perform smc write       */;\
  restore_flags()                                                            ;\
  bx lr                                   /* return                        */;\
                                                                             ;\
thumb_label(ext_store_ewram_u##store_type)                                   ;\
  save_flags()                                                               ;\
  mask_addr_##store_type(18)              /* Mask to mirror memory (+align)*/;\
  add r2, reg_base, #EWRAM_OFF            /* r2 = ewram base               */;\
  str_op r1, [r0, r2]                     /* store data                    */;\
  add r2, r2, #0x40000                    /* r2 = ewram smc base           */;\
  load_op r1, [r0, r2]                    /* r1 = SMC sentinel             */;\
  cmp r1, #0                              /* Check value, should be zero   */;\
  bne 3f                                  /* if so perform smc write       */;\
  restore_flags()                                                            ;\
  bx lr                                   /* return                        */;\
                                                                             ;\
thumb_label(ext_store_vram_u##store_type)                                    ;\
  save_flags()                                                               ;\
  mask_addr_bus16_##store_type(17)        /* Mask to mirror memory (+align)*/;\
  dup##store_type(r1)                     /* Duplicate byte if necessary   */;\
  cmp r0, #0x18000                        /* Check if exceeds 96KB         */;\
  it cs                                                                      ;\
  subcs r0, r0, #0x8000                   /* Mirror to the last bank       */;\
  add r2, reg_base, #VRAM_OFF             /* r2 = vram base                */;\
  restore_flags()                                                            ;\
  str_op16 r1, [r0, r2]                   /* store data                    */;\
  bx lr                                   /* return                        */;\
                                                                             ;\
thumb_label(ext_store_oam_ram_u##store_type)                                 ;\
  mask_addr_bus16_##store_type(10)        /* Mask to mirror memory (+align)*/;\
  dup##store_type(r1)                     /* Duplicate byte if necessary   */;\
  add r2, reg_base, #OAM_RAM_OFF          /* r2 = oam ram base             */;\
  str_op16 r1, [r0, r2]                   /* store data                    */;\
  str r2, [reg_base, #OAM_UPDATED]        /* write non zero to signal      */;\
  bx lr                                   /* return                        */;\
                                                                             ;\
3: /* Flush RAM cache and "resume" execution via re-compile */               ;\
  str reg_gpc, [reg_base, #REG_PC]        /* write out PC                  */;\
  call_c_function(flush_translation_cache_ram)                               ;\
  b resume_pc                             /* continue execution            */;\

@ for ignored areas, just return
thumb_label(ext_store_ignore)
  bx lr                                   @ return

@ SWI 6/7 division (HLE): guest r0/r1 live in x0/x1 (same slot in both ARM
@ and Thumb allocations). Quotient -> r0, remainder -> r1, abs(quotient) ->
@ r3. Uses the hardware SDIV (the ARM backend emits an unrolled divider).
@ divarm (SWI 7) takes the operands swapped.

defsymbl(t2_hle_divarm)
  mov r2, reg_x0
  mov reg_x0, reg_x1
  mov reg_x1, r2
defsymbl(t2_hle_div)
  save_flags()
  sdiv r0, reg_x0, reg_x1                 @ r0 = quotient
  mls r1, r0, reg_x1, reg_x0              @ r1 = remainder
  mov reg_x0, r0
  mov reg_x1, r1
  eor r2, r0, r0, asr #31                 @ r2 = abs(quotient)...
  sub r2, r2, r0, asr #31                 @ ...branchless
  str r2, [reg_base, #REG_R3]             @ guest r3 (memory slot)
  ldr r1, [reg_base, #REG_CPSR]
  tst r1, #0x20                           @ Thumb guest: r3 is cached in x3
  it ne
  movne reg_x3, r2
  restore_flags()
  bx lr
.size t2_hle_div, .-t2_hle_div

#define store_lookup_table(store_type)                                       ;\
  .word ext_store_ignore                  /*   -1: ignore, for > 0x0F      */;\
  .word ext_store_ignore                  /* 0x00: BIOS, ignore            */;\
  .word ext_store_ignore                  /* 0x01: ignore                  */;\
  .word ext_store_ewram_u##store_type     /* 0x02: ewram                   */;\
  .word ext_store_iwram_u##store_type     /* 0x03: iwram                   */;\
  .word ext_io_store_u##store_type        /* 0x04: I/O regs                */;\
  .word ext_store_palette_u##store_type   /* 0x05: palette RAM             */;\
  .word ext_store_vram_u##store_type      /* 0x06: vram                    */;\
  .word ext_store_oam_ram_u##store_type   /* 0x07: oam ram                 */;\
  .word ext_store_u##store_type           /* 0x08: gamepak: ignore         */;\
  .word ext_store_ignore                  /* 0x09: gamepak: ignore         */;\
  .word ext_store_ignore                  /* 0x0A: gamepak: ignore         */;\
  .word ext_store_ignore                  /* 0x0B: gamepak: ignore         */;\
  .word ext_store_ignore                  /* 0x0C: gamepak: ignore         */;\
  .word ext_store_u##store_type           /* 0x0D: EEPROM                  */;\
  .word ext_store_u##store_type           /* 0x0E: backup                  */;\
  .word ext_store_ignore                  /* 0x0F: ignore                  */;\

execute_store_builder(8,  strb, strh, ldrb)
execute_store_builder(16, strh, strh, ldrh)
execute_store_builder(32, str,  str,  ldr )

@ Palette writes are special since they are converted on the fly for speed

thumb_label(ext_store_palette_u8)
  bic r1, r1, #0xff00                     @ Duplicate the byte
  orr r1, r1, r1, lsl #8
thumb_label(ext_store_palette_u16)
  mask_addr_16(10)                        @ Accesses are always 16 bit
  add r2, reg_base, #PAL_RAM_OFF          @ r2 = palette base
  strh r1, [r0, r2]                       @ store data

  and r2, r1, #0x3E0                      @ Convert color point
  lsl r2, r2, #1
  orr r2, r2, r1, lsl #11
  and r1, r1, #0x7C00
  orr r2, r2, r1, lsr #10

  add r1, reg_base, #PAL_CONV_OFF         @ r1 = converted palette ram
  strh r2, [r0, r1]                       @ Converted value write (r2)
  bx lr                                   @ return

thumb_label(ext_store_palette_u32_safe)
thumb_label(ext_store_palette_u32)
  mask_addr_32(10)                        @ Accesses are always 16 bit
  add r2, reg_base, #PAL_RAM_OFF          @ r2 = palette base
  str r1, [r0, r2]                        @ store data
  add r2, reg_base, #PAL_CONV_OFF         @ r2 = converted palette ram

  lsr r9, r1, #10
  and r9, r9, #0x1F
  orr r9, r9, r1, lsl #11
  bic r1, r1, #0x1F
  bic r1, r1, #0xFC00
  orr r9, r9, r1, lsl #1
  strh r9, [r0, r2]                       @ Write first halfword
  add r0, r0, #2

  lsr r1, r1, #16
  and r9, r1, #0x3E0
  lsl r9, r9, #1
  orr r9, r9, r1, lsl #11
  and r1, r1, #0x7C00
  orr r9, r9, r1, lsr #10
  strh r9, [r0, r2]                       @ Write second halfword

  bx lr                                   @ return

@ This is a store that is executed in a strm case (so no SMC checks in-between)

thumb_label(ext_store_u32_safe)
thumb_label(ext_io_store_u32_safe)
  save_flags()
  call_c_function(write_memory32)         @ Perform 32bit store
  restore_flags()
  bx lr                                   @ Return

thumb_label(ext_store_iwram_u32_safe)
  mask_addr_8(15)                         @ Mask to mirror memory (no need to align!)
  add r2, reg_base, #(IWRAM_OFF+0x8000)   @ r2 = iwram base
  str r1, [r0, r2]                        @ store data
  bx lr                                   @ Return

thumb_label(ext_store_ewram_u32_safe)
  mask_addr_8(18)                         @ Mask to mirror memory (no need to align!)
  add r2, reg_base, #EWRAM_OFF            @ r2 = ewram base
  str r1, [r0, r2]                        @ store data
  bx lr                                   @ Return

thumb_label(ext_store_vram_u32_safe)
  mask_addr_8(17)                         @ Mask to mirror memory (no need to align!)
  save_flags()
  add r2, reg_base, #VRAM_OFF             @ r2 = vram base
  cmp r0, #0x18000                        @ Check if exceeds 96KB
  it cs
  subcs r0, r0, #0x8000                   @ Mirror to the last bank
  str r1, [r0, r2]                        @ store data
  restore_flags()
  bx lr                                   @ Return

thumb_label(ext_store_oam_ram_u32_safe)
  mask_addr_8(10)                         @ Mask to mirror memory (no need to align!)
  add r2, reg_base, #OAM_RAM_OFF          @ r2 = oam ram base
  str r1, [r0, r2]                        @ store data
  str r2, [reg_base, #OAM_UPDATED]        @ store anything non zero here
  bx lr                                   @ Return

write_epilogue:
  ldr r2, [reg_base, #REG_CPSR]           @ Save all register and CPSR
  tst r2, #0x20                           @ Check thumb bit
  store_registers_cond()                  @ Store ARM/Thumb regs
  collapse_flags(r1)                      @ interrupt needs current flags

  mov r2, r0                              @ r2 is stored across C calls
  tst r2, #CPU_ALERT_SMC                  @ check for SMC code
  beq 1f
  call_c_function(flush_translation_cache_ram)   @ Flush RAM if bit is set

1:
  tst r2, #CPU_ALERT_IRQ                  @ check for IRQs
  beq 2f
  call_c_function(check_and_raise_interrupts)    @ Update CPU state to raise IRQ

2:
  tst r2, #CPU_ALERT_HALT                 @ check for CPU halt bit
  beq lookup_pc                           @ Resume execution if not halted

  @ Fallthrough to cpu_sleep_loop on purpose (CPU is now halted)

  mvn r0, reg_cycles                      @ setup for update_gba

cpu_sleep_loop:
  call_c_function(update_gba)             @ update GBA until CPU isn't halted

  cmp r0, #0
  blt return_to_main                      @ New frame if bit 31 is set. Exit

  bic r0, r0, #0xF0000000                 @ clear MSB, not part of count

  @ The cpu is active again, go ahead and resume execution at current PC
  mvn reg_cycles, r0                      @ load new cycle count
  ldr r0, [reg_base, #REG_PC]             @ load new PC
  ldr r1, [reg_base, #REG_CPSR]           @ r1 = flags
  tst r1, #0x20                           @ see if Thumb bit is set
  bne 2f

  call_c_function(block_lookup_address_arm)
  load_registers_arm()
  extract_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0                                   @ jump to new ARM block

2:
  call_c_function(block_lookup_address_thumb)
  load_registers_thumb()
  extract_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0                                   @ jump to new Thumb block

resume_pc:
  @ Resume regular execution (except we might need to recompile due to flush)
  @ assume flags are spilled to reg_flags
  ldr r0, [reg_base, #REG_PC]             @ r0 = new pc
  ldr r1, [reg_base, #REG_CPSR]           @ r1 = partial flags valid
  tst r1, #0x20                           @ see if Thumb bit is set
  beq 1f                                  @ if not lookup ARM

  call_c_function(block_lookup_address_thumb)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0                                   @ jump to new Thumb block
1:
  call_c_function(block_lookup_address_arm)
  restore_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0                                   @ jump to new ARM block

lookup_pc:
  @ Restart CPU execution, assumes CPU mode might have changed
  ldr r0, [reg_base, #REG_PC]             @ r0 = new pc
  ldr r1, [reg_base, #REG_CPSR]           @ r1 = flags
  tst r1, #0x20                           @ see if Thumb bit is set
  beq 2f                                  @ if not lookup ARM

  call_c_function(block_lookup_address_thumb)
  load_registers_thumb()
  extract_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0                                   @ jump to new Thumb block
2:
  call_c_function(block_lookup_address_arm)
  load_registers_arm()
  extract_flags()
  orr r0, r0, #1                          @ Thumb bit for block entry
  bx r0                                   @ jump to new ARM block

@ T32 register-offset loads only allow LSL; addresses are pre-masked with
@ UBFX (no flags disturbed).

#define exec_ld_op_s8(mirrorbits)                                            ;\
  ubfx r0, r0, #0, #(mirrorbits)                                               ;\
  ldrsb r0, [r2, r0]

#define exec_ld_op_u8(mirrorbits)                                            ;\
  ubfx r0, r0, #0, #(mirrorbits)                                               ;\
  ldrb r0, [r2, r0]

#define exec_ld_op_s16(mirrorbits)                                           ;\
  ubfx r0, r0, #0, #(mirrorbits)                                               ;\
  bic r0, r0, #1                                                             ;\
  ldrsh r0, [r2, r0]

#define exec_ld_op_u16(mirrorbits)                                           ;\
  ubfx r0, r0, #0, #(mirrorbits)                                               ;\
  bic r0, r0, #1                                                             ;\
  ldrh r0, [r2, r0]

#define exec_ld_op_u32(mirrorbits)                                           ;\
  ubfx r0, r0, #0, #(mirrorbits)                                               ;\
  bic r0, r0, #3                                                             ;\
  ldr r0, [r2, r0]

@ Input:
@ r0: address
@ reg_gpc: current guest PC (slow paths / BIOS verification)

#define execute_load_builder(load_type, albits, load_function)               ;\
                                                                             ;\
thumb_label(ld_bios_##load_type)          /* BIOS area, need to verify PC  */;\
  save_flags()                                                               ;\
  lsrs r1, reg_gpc, #15                   /* r1 = High addr bits from PC   */;\
  bne 10f                                 /* Jump to slow handler          */;\
  ldr r2, [reg_base, #RDMAP_OFF]          /* r2 = read_mem[0]              */;\
  exec_ld_op_##load_type(15)              /* Clear upper bits (15 LSB)     */;\
  restore_flags()                                                            ;\
  bx lr                                                                      ;\
                                                                             ;\
thumb_label(ld_ewram_##load_type)         /* EWRAM area                    */;\
  add r2, reg_base, #EWRAM_OFF                                               ;\
  exec_ld_op_##load_type(18)              /* Clear upper bits (18 LSB)     */;\
  bx lr                                                                      ;\
                                                                             ;\
thumb_label(ld_iwram_##load_type)         /* IWRAM area                    */;\
  add r2, reg_base, #(IWRAM_OFF+0x8000)                                      ;\
  exec_ld_op_##load_type(15)              /* Clear upper bits (15 LSB)     */;\
  bx lr                                                                      ;\
                                                                             ;\
thumb_label(ld_ioram_##load_type)         /* I/O RAM area                  */;\
  add r2, reg_base, #IOREG_OFF                                               ;\
  exec_ld_op_##load_type(10)              /* Clear upper bits (10 LSB)     */;\
  bx lr                                                                      ;\
                                                                             ;\
thumb_label(ld_palram_##load_type)        /* Palette RAM area              */;\
  add r2, reg_base, #PAL_RAM_OFF                                             ;\
  exec_ld_op_##load_type(10)              /* Clear upper bits (10 LSB)     */;\
  bx lr                                                                      ;\
                                                                             ;\
thumb_label(ld_oamram_##load_type)        /* OAM RAM area                  */;\
  add r2, reg_base, #OAM_RAM_OFF                                             ;\
  exec_ld_op_##load_type(10)              /* Clear upper bits (10 LSB)     */;\
  bx lr                                                                      ;\
                                                                             ;\
/* ROM area: uses generic memory handlers to handle swapping */              ;\
thumb_label(ld_rdmap_slow_##load_type)                                       ;\
  save_flags()                                                               ;\
  add r2, reg_base, #RDMAP_OFF            /* r2 = memory_map_read          */;\
  lsr r1, r0, #15                         /* r1 = page index of address    */;\
  ldr r2, [r2, r1, lsl #2]                /* r2 = base addr                */;\
  cmp r2, #0                                                                 ;\
  beq 9f                                  /* Page miss, go slow            */;\
                                                                             ;\
  exec_ld_op_##load_type(15)              /* Pages are 32KB big            */;\
  restore_flags()                                                            ;\
  bx lr                                                                      ;\
                                                                             ;\
/* ROM/VRAM area: uses generic memory handlers, assumes is mapped */         ;\
thumb_label(ld_rdmap_##load_type)                                            ;\
  add r2, reg_base, #RDMAP_OFF            /* r2 = memory_map_read          */;\
  lsr r1, r0, #15                         /* r1 = page index of address    */;\
  ldr r2, [r2, r1, lsl #2]                /* r2 = base addr                */;\
                                                                             ;\
  exec_ld_op_##load_type(15)              /* Pages are 32KB big            */;\
  bx lr                                                                      ;\
                                                                             ;\
/* Slow load path, for open/unmapped loads                                 */;\
thumb_label(ld_slow_##load_type)                                             ;\
  save_flags()                                                               ;\
9:                                                                           ;\
10:                                                                          ;\
  str reg_gpc, [reg_base, #REG_PC]        /* update PC                     */;\
  call_c_function(load_function)                                             ;\
  restore_flags()                                                            ;\
  bx lr                                   /* return                        */;\

#define load_table_gen(load_type, rdmapfn)                                   ;\
  .word ld_slow_##load_type               /* -1 (for regions above F)      */;\
  .word ld_bios_##load_type               /* 0 BIOS                        */;\
  .word ld_slow_##load_type               /* 1 Bad region                  */;\
  .word ld_ewram_##load_type              /* 2 EWRAM                       */;\
  .word ld_iwram_##load_type              /* 3 IWRAM                       */;\
  .word ld_ioram_##load_type              /* 4 I/O                         */;\
  .word ld_palram_##load_type             /* 5 Palette RAM, via map        */;\
  .word ld_rdmap_##load_type              /* 6 VRAM area                   */;\
  .word ld_oamram_##load_type             /* 7 OAM RAM                     */;\
  .word ld_##rdmapfn##_##load_type        /* 8 ROM, via map                */;\
  .word ld_##rdmapfn##_##load_type        /* 9 ROM, via map                */;\
  .word ld_##rdmapfn##_##load_type        /* A ROM, via map                */;\
  .word ld_##rdmapfn##_##load_type        /* B ROM, via map                */;\
  .word ld_##rdmapfn##_##load_type        /* C ROM, via map                */;\
  .word ld_slow_##load_type               /* D ROM or EEPROM/FLASH         */;\
  .word ld_slow_##load_type               /* E EEPROM/FLASH                */;\
  .word ld_slow_##load_type               /* F Bad region                  */;\

execute_load_builder(u8,  0, read_memory8  )
execute_load_builder(s8,  0, read_memory8s )
execute_load_builder(u16, 1, read_memory16 )
execute_load_builder(s16, 1, read_memory16s)
execute_load_builder(u32, 2, read_memory32 )

.data
.align 4

defsymbl_data(st_handler_functions)
  store_lookup_table(8)
  store_lookup_table(16)
  store_lookup_table(32)
  store_lookup_table(32_safe)
defsymbl_data(ld_handler_functions)
  load_table_gen(u8,  rdmap)
  load_table_gen(s8,  rdmap)
  load_table_gen(u16, rdmap)
  load_table_gen(s16, rdmap)
  load_table_gen(u32, rdmap)
defsymbl_data(ld_swap_handler_functions)
  load_table_gen(u8,  rdmap_slow)
  load_table_gen(s8,  rdmap_slow)
  load_table_gen(u16, rdmap_slow)
  load_table_gen(s16, rdmap_slow)
  load_table_gen(u32, rdmap_slow)

.bss
.align 4

defsymbl_data(iwram)
  .space 0x10000
defsymbl_data(vram)
  .space 0x18000
defsymbl_data(ewram)
  .space 0x80000
defsymbl_data(reg)
  .space 0x100
defsymbl_data(spsr)
  .space 24
@ Place lookup tables here for easy access via base_reg too
defsymbl_data(st_lookup_tables)
  .space 4*17*4   @ store
defsymbl_data(ld_lookup_tables)
  .space 5*17*4   @ loads
  .space 132      @ Padding for alignment
defsymbl_data(reg_mode)
  .space 196
  .space 60       @ More padding!
defsymbl_data(oam_ram)
  .space 0x400
defsymbl_data(palette_ram)
  .space 0x400
defsymbl_data(memory_map_read)
  .space 0x8000
defsymbl_data(io_registers)
  .space 0x400
defsymbl_data(palette_ram_converted)
  .space 0x400

@ Translation caches: plain .bss (PSRAM is executable on the Playdate,
@ verified by pd_jit_smoke.c; a custom section would be orphaned by the
@ SDK link script).
defsymbl_data(rom_translation_cache)
  .space ROM_TRANSLATION_CACHE_SIZE
.size rom_translation_cache, .-rom_translation_cache
defsymbl_data(ram_translation_cache)
  .space RAM_TRANSLATION_CACHE_SIZE
.size ram_translation_cache, .-ram_translation_cache
