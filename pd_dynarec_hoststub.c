/* Host-side (simulator) stand-in for arm/thumb2_stub.S, for DYNAREC builds
 * of the SIMULATOR only. Purpose: let the translator (cpu_threaded.c, pure
 * C) run on the host so its emitted Thumb-2 can be dumped and disassembled
 * offline (PD_TRANSLATE_DUMP in playdate_main.c). The emitted code is NEVER
 * executed here - these symbols only need to exist and have the right
 * sizes. Device builds use the real assembly stub. */

#if defined(TARGET_SIMULATOR) && defined(HAVE_DYNAREC)

#include "common.h"
#include "gpsp_config.h"

/* Guest memory + tables (the device stub places these around reg_base;
 * layout only matters to emitted code, which never runs on the host). */
u8 iwram[1024 * 32 * 2];
u8 vram[1024 * 96];
u8 ewram[1024 * 256 * 2];
u32 reg[64];
u32 spsr[6];
u32 st_lookup_tables[4][17];
u32 ld_lookup_tables[5][17];
u32 reg_mode[7][7];
u16 oam_ram[512];
u16 palette_ram[512];
u8 *memory_map_read[8 * 1024];
u16 io_registers[512];
u16 palette_ram_converted[512];

u32 st_handler_functions[4][17];
u32 ld_handler_functions[5][17];
u32 ld_swap_handler_functions[5][17];

u8 rom_translation_cache[ROM_TRANSLATION_CACHE_SIZE];
u8 ram_translation_cache[RAM_TRANSLATION_CACHE_SIZE];

/* Stub entry points referenced by init_emitter's function table. */
void arm_update_gba_arm(void) {}
void arm_update_gba_thumb(void) {}
void arm_update_gba_idle_arm(void) {}
void arm_update_gba_idle_thumb(void) {}
void arm_cheat_hook(void) {}
void thumb_cheat_hook(void) {}
void arm_indirect_branch_arm(void) {}
void arm_indirect_branch_thumb(void) {}
void arm_indirect_branch_dual_arm(void) {}
void arm_indirect_branch_dual_thumb(void) {}
void execute_store_cpsr(void) {}
void execute_spsr_restore(void) {}
void execute_swi_arm(void) {}
void execute_swi_thumb(void) {}
void t2_hle_div(void) {}
void t2_hle_divarm(void) {}

u32 execute_arm_translate_internal(u32 cycles, void *regptr)
{
  (void)cycles; (void)regptr;
  return 0; /* never called: dynarec_enable stays 0 on the host */
}

#endif /* TARGET_SIMULATOR && HAVE_DYNAREC */
