/* Native Thumb PUSH fast path. The normal generated handlers remain the
 * fallback whenever the guest stack does not map wholly to IWRAM. */

#ifdef PD_IWRAM_STACK_FAST

#include "common.h"

u32 pd_iwram_stack_fast_active;

u32 pd_iwram_stack_push(u32 encoded)
{
  u32 reg_list = encoded & 0xFFU;
  u32 include_lr = (encoded >> 8) & 1U;
  u32 count = bit_count[reg_list] + include_lr;
  u32 old_sp, new_sp, address;
  u32 i;

  if (!count)
    return 0;

  old_sp = reg[REG_SP] & ~3U;
  new_sp = old_sp - count * 4U;
  if ((new_sp >> 24) != 3U || ((old_sp - 1U) >> 24) != 3U)
    return 0;

  address = new_sp;
  for (i = 0; i < 8; i++)
    if (reg_list & (1U << i))
    {
      address32(iwram + 0x8000, address & 0x7FFFU) = reg[i];
      address += 4;
    }

  if (include_lr)
    address32(iwram + 0x8000, address & 0x7FFFU) = reg[REG_LR];

  reg[REG_SP] = new_sp;
  pd_iwram_stack_fast_active = 1;
  return 1;
}

#endif /* PD_IWRAM_STACK_FAST */
