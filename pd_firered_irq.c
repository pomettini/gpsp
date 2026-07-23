/* FireRed US 1.0 HBlank IRQ bridge.
 *
 * Battle transitions install a per-scanline callback and therefore run the
 * BIOS IRQ wrapper, FireRed's copied ARM dispatcher and its generic Thumb
 * HBlank wrapper hundreds of times per frame.  Keep executing the dynamic
 * callback as guest code, but perform the invariant save/ack/dispatch/return
 * work natively.  Exact RAM and ROM signatures keep this game-specific path
 * inert for every other title and FireRed revision. */

#ifdef PD_FIRERED_IRQ_HLE

#include <string.h>

#include "common.h"
#include "pd_firered_irq.h"

#define FR_INTR_MAIN_OFF       0x00003580U
#define FR_INTR_TABLE_OFF      0x00003540U
#define FR_HBLANK_SLOT_OFF     (FR_INTR_TABLE_OFF + 4U)
#define FR_MAIN_OFF            0x000030F0U
#define FR_HBLANK_CB_OFF       (FR_MAIN_OFF + 24U)
#define FR_INTR_CHECK_OFF      (FR_MAIN_OFF + 28U)
#define FR_BIOS_INTR_CHECK_OFF 0x00007FF8U

#define FR_HBLANK_WRAPPER      0x08000845U

typedef struct
{
  u32 regs[16];
  u32 cpsr;
  u32 mode;
  u16 ime;
  u16 reserved;
} PDFireRedIRQState;

static PDFireRedIRQState pd_fr_irq_state;
static u32 pd_fr_irq_active;
u32 pd_firered_irq_matched;

static u8 *fr_iwram(void)
{
  /* The first 32KB is the dynarec SMC shadow; guest IWRAM follows. */
  return iwram + 0x8000;
}

static int fr_irq_signatures_match(void)
{
  u8 *ram = fr_iwram();

  if (readaddress32(ram, FR_INTR_MAIN_OFF + 0x00) != 0xE3A03301U ||
      readaddress32(ram, FR_INTR_MAIN_OFF + 0x04) != 0xE2833C02U ||
      readaddress32(ram, FR_INTR_MAIN_OFF + 0x08) != 0xE5932000U ||
      readaddress32(ram, FR_HBLANK_SLOT_OFF) != FR_HBLANK_WRAPPER)
    return 0;

  if (read_memory32(0x08000844U) != 0x4C09B510U ||
      read_memory32(0x08000864U) != 0xBC01BC10U)
    return 0;

  pd_firered_irq_matched = 1;
  return 1;
}

static void fr_irq_set_completion_flags(void)
{
  u8 *ram = fr_iwram();
  address16(ram, FR_BIOS_INTR_CHECK_OFF) |= IRQ_HBLANK;
  address16(ram, FR_INTR_CHECK_OFF) |= IRQ_HBLANK;
}

/* Returns 0 for the ordinary IRQ path, 1 when a guest callback was entered,
 * and 2 when an empty callback was handled entirely in C. */
int pd_firered_irq_try_enter(void)
{
  u32 pending = read_ioreg(REG_IE) & read_ioreg(REG_IF);
  u32 callback;
  u32 i;
  u8 *ram;

  if (pd_fr_irq_active || pending != IRQ_HBLANK ||
      (!pd_firered_irq_matched && !fr_irq_signatures_match()))
    return 0;

  ram = fr_iwram();
  callback = readaddress32(ram, FR_HBLANK_CB_OFF);

  for (i = 0; i < 16; i++)
    pd_fr_irq_state.regs[i] = reg[i];
  pd_fr_irq_state.cpsr = reg[REG_CPSR];
  pd_fr_irq_state.mode = reg[CPU_MODE];
  pd_fr_irq_state.ime = read_ioreg(REG_IME);
  pd_fr_irq_active = 1;

  /* IntrMain acknowledges HBlank before calling the registered callback and
   * keeps master interrupts disabled until it returns. */
  write_ioreg(REG_IF, read_ioreg(REG_IF) & ~IRQ_HBLANK);
  write_ioreg(REG_IME, 0);

  if (!callback)
  {
    fr_irq_set_completion_flags();
    write_ioreg(REG_IME, pd_fr_irq_state.ime);
    pd_fr_irq_active = 0;
    return 2;
  }

  set_cpu_mode(MODE_SYSTEM);
  reg[REG_CPSR] = 0x1FU | ((callback & 1U) ? 0x20U : 0U);

  /* IntrMain saves its link register on the system stack before BLX.  Match
   * that stack shape for callbacks which expect the usual 8-byte alignment;
   * the complete interrupted register set is restored by the return bridge. */
  reg[REG_SP] -= 4;
  if ((reg[REG_SP] >> 24) == 3)
    address32(ram, reg[REG_SP] & 0x7FFF) = pd_fr_irq_state.regs[REG_LR];
  else
    write_memory32(reg[REG_SP], pd_fr_irq_state.regs[REG_LR]);

  reg[0] = callback;
  reg[REG_LR] = PD_FIRERED_IRQ_RETURN_PC | 1U;
  reg[REG_PC] = callback & ~1U;
  return 1;
}

void pd_firered_irq_return(void)
{
  u32 i;

  if (!pd_fr_irq_active)
    return;

  fr_irq_set_completion_flags();
  write_ioreg(REG_IME, pd_fr_irq_state.ime);

  set_cpu_mode(pd_fr_irq_state.mode);
  for (i = 0; i < 16; i++)
    reg[i] = pd_fr_irq_state.regs[i];
  reg[REG_CPSR] = pd_fr_irq_state.cpsr;
  reg[CPU_MODE] = pd_fr_irq_state.mode;
  pd_fr_irq_active = 0;
}

#endif /* PD_FIRERED_IRQ_HLE */
