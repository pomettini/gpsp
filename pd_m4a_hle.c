/* Native fast path for the hottest inner loop of FireRed's runtime-copied
 * m4a SoundMainRAM mixer. The translator enables this only when the exact
 * captured instruction sequence matches; unusual output pointers fall back
 * after the loop's two initial loads. Looping PCM sample boundaries are
 * wrapped in place; non-looping endings return to the original guest code.
 * Each call follows the dynarec's current cycle budget and can overshoot it
 * by at most one four-sample group. */

#ifdef PD_M4A_HLE

#include "common.h"

#define M4A_INNER_PC       0x03002BECU
#define M4A_INNER_BODY_OFF 0x00002B6CU
#define M4A_INNER_BODY_LEN 236U
#define M4A_INNER_FNV1A    0xACB5F1E0U
#define M4A_PCM_STRIDE     0x630U
#define M4A_MAX_GROUPS     32U

u32 pd_m4a_hle_matched;

int pd_m4a_hle_matches(void)
{
  const u8 *p = iwram + 0x8000 + M4A_INNER_BODY_OFF;
  u32 hash = 2166136261U;
  u32 i;

  for (i = 0; i < M4A_INNER_BODY_LEN; i++)
    hash = (hash ^ p[i]) * 16777619U;

  if (hash == M4A_INNER_FNV1A)
  {
    pd_m4a_hle_matched = 1;
    return 1;
  }
  return 0;
}

static s32 m4a_load_s8(u32 address)
{
  u8 *map = NULL;

  if (address < 0x10000000U)
    map = memory_map_read[address >> 15];
  if (map)
    return (s8)readaddress8(map, address & 0x7FFF);
  return (s8)read_memory8(address);
}

static u32 m4a_ror8(u32 value)
{
  return (value >> 8) | (value << 24);
}

/* AAPCS returns this u64 in r0:r1: next guest PC, then charged cycles. */
u64 pd_m4a_hle_inner(u32 cycle_budget)
{
  u32 r0 = reg[0], r1 = reg[1], r2 = reg[2], r3 = reg[3];
  u32 r4 = reg[4], r5 = reg[5], r6 = reg[6], r7 = reg[7], r8 = reg[8];
  u32 r9 = reg[9], r10 = reg[10], r11 = reg[11];
  u32 cycles = 0, groups = 0;
  u32 next_pc;

  /* At the word-loop entry r5 is aligned and points into guest IWRAM.
   * Execute the two original loads and resume normally if that invariant
   * ever differs, instead of applying FireRed-specific address shortcuts. */
  if ((r5 >> 24) != 3 || (r5 & 3) != 0 ||
      ((r5 + M4A_PCM_STRIDE) >> 24) != 3)
  {
    r6 = read_memory32(r5);
    r7 = read_memory32(r5 + M4A_PCM_STRIDE);
    reg[6] = r6;
    reg[7] = r7;
    cycles = 6;
    next_pc = M4A_INNER_PC + 8;
    return ((u64)cycles << 32) | next_pc;
  }

  while ((s32)r8 > 0 && groups < M4A_MAX_GROUPS &&
         (groups == 0 || cycles < cycle_budget))
  {
    u32 out_off = r5 & 0x7FFF;
    u32 lane;

    r6 = readaddress32(iwram + 0x8000, out_off);
    r7 = readaddress32(iwram + 0x8000,
                       (out_off + M4A_PCM_STRIDE) & 0x7FFF);
    cycles += 6; /* two guest LDRs, including memory-access charges */

    for (lane = 0; lane < 4; lane++)
    {
      u32 product = (u32)((u64)r9 * r1);
      s32 sample = (s32)r0 + ((s32)product >> 23);
      u32 mix;
      u32 step;
      u32 old_r5;

      mix = (u32)((u64)r10 * (u32)sample) & ~0x00FF0000U;
      r6 = mix + m4a_ror8(r6);
      mix = (u32)((u64)r11 * (u32)sample) & ~0x00FF0000U;
      r7 = mix + m4a_ror8(r7);

      r9 += r4;
      step = r9 >> 23;
      cycles += 17; /* MUL..MOVS plus the BEQ */

      if (step)
      {
        u32 loop_length;

        r9 &= ~0x3F800000U;
        r2 -= step;
        cycles += 3; /* BIC, SUBS, BLE */
        if ((s32)r2 <= 0)
        {
          u32 loop_offset;

          loop_length = read_memory32(reg[REG_SP] + 24);
          cycles += 5; /* LDR, CMP and BEQ */
          if (loop_length == 0)
          {
            reg[0] = r0;
            reg[1] = r1;
            reg[2] = r2;
            reg[3] = r3;
            reg[5] = r5;
            reg[6] = r6;
            reg[7] = r7;
            reg[8] = r8;
            reg[9] = r9;
            next_pc = 0x03002B90U;
            return ((u64)cycles << 32) | next_pc;
          }

          r3 = read_memory32(reg[REG_SP] + 20);
          loop_offset = 0U - r2;
          cycles += 4; /* LDR and RSB */
          do
          {
            r2 += loop_length;
            cycles += 2; /* ADDS and BGT */
            if ((s32)r2 > 0)
              break;
            loop_offset -= loop_length;
            cycles += 2; /* SUB and B */
          }
          while (1);

          r3 += loop_offset;
          r0 = (u32)m4a_load_s8(r3);
          r3++;
          r1 = (u32)m4a_load_s8(r3);
          r1 -= r0;
          cycles += 7; /* two LDRSBs and SUB */
          goto m4a_advance_output;
        }

        step--;
        cycles += 2; /* SUBS and conditional ADD */
        if (step == 0)
        {
          r0 += r1;
          cycles += 1; /* failed conditional LDRSB */
        }
        else
        {
          r3 += step;
          r0 = (u32)m4a_load_s8(r3);
          cycles += 3;
        }
        r3++;
        r1 = (u32)m4a_load_s8(r3);
        r1 -= r0;
        cycles += 4; /* second LDRSB and SUB */
      }

m4a_advance_output:
      old_r5 = r5;
      r5 += 0x40000000U;
      cycles += 2; /* ADDS and BCC */
      if (r5 < old_r5)
        break;
    }

    /* Four lane rotations restore normal byte order before the stores. */
    out_off = r5 & 0x7FFF;
    address32(iwram + 0x8000,
              (out_off + M4A_PCM_STRIDE) & 0x7FFF) = r7;
    address32(iwram + 0x8000, out_off) = r6;
    r5 += 4;
    r8 -= 4;
    groups++;
    cycles += 6; /* two STRs, SUBS and BGT */
  }

  reg[0] = r0;
  reg[1] = r1;
  reg[2] = r2;
  reg[3] = r3;
  reg[5] = r5;
  reg[6] = r6;
  reg[7] = r7;
  reg[8] = r8;
  reg[9] = r9;

  next_pc = ((s32)r8 > 0) ? M4A_INNER_PC : 0x03002C58U;
  return ((u64)cycles << 32) | next_pc;
}

#endif /* PD_M4A_HLE */
