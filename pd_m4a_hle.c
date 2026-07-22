/* Native fast path for the hottest inner loop of FireRed's runtime-copied
 * m4a SoundMainRAM mixer. The translator enables this only when the exact
 * captured instruction sequence matches; unusual output pointers fall back
 * after the loop's two initial loads. The final signed-byte mixdown is a
 * second exact-signature path. Each call follows the dynarec's current cycle
 * budget and can overshoot it by at most one original loop iteration. */

#ifdef PD_M4A_HLE

#include "common.h"

#define M4A_INNER_PC       0x03002BECU
#define M4A_INNER_BODY_OFF 0x00002BD4U
#define M4A_INNER_BODY_LEN 132U
#define M4A_INNER_FNV1A    0x678FE071U
#define M4A_MIX_PC         0x030028FCU
#define M4A_MIX_BODY_OFF   0x000028FCU
#define M4A_MIX_BODY_LEN   60U
#define M4A_MIX_FNV1A      0x9296CA9BU
#define M4A_PCM_STRIDE     0x630U
#define M4A_MAX_GROUPS     32U
#define M4A_MIX_MAX_SAMPLES 256U

u32 pd_m4a_hle_matched;

int pd_m4a_hle_matches(u32 pc)
{
  u32 offset, length, expected;
  const u8 *p;
  u32 hash = 2166136261U;
  u32 i;

  if (pc == M4A_INNER_PC)
  {
    offset = M4A_INNER_BODY_OFF;
    length = M4A_INNER_BODY_LEN;
    expected = M4A_INNER_FNV1A;
  }
  else if (pc == M4A_MIX_PC)
  {
    offset = M4A_MIX_BODY_OFF;
    length = M4A_MIX_BODY_LEN;
    expected = M4A_MIX_FNV1A;
  }
  else
    return 0;

  p = iwram + 0x8000 + offset;
  for (i = 0; i < length; i++)
    hash = (hash ^ p[i]) * 16777619U;

  if (hash == expected)
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
        r9 &= ~0x3F800000U;
        r2 -= step;
        cycles += 3; /* BIC, SUBS, BLE */
        if ((s32)r2 <= 0)
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
          next_pc = 0x03002B6CU;
          return ((u64)cycles << 32) | next_pc;
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

/* FireRed's final four-tap signed-byte mixdown at 0x030028FC. */
u64 pd_m4a_hle_mixdown(u32 cycle_budget)
{
  u32 r0 = reg[0], r1 = reg[1], r3 = reg[3], r4 = reg[4];
  u32 r5 = reg[5], r6 = reg[6], r7 = reg[7];
  u32 cycles = 0, samples = 0;
  u32 next_pc;

  if ((s32)r4 <= 0 || (r5 >> 24) != 3 || ((r5 + r6) >> 24) != 3 ||
      (r7 >> 24) != 3 || ((r7 + r6) >> 24) != 3)
  {
    reg[0] = (u32)m4a_load_s8(r5 + r6);
    return ((u64)3 << 32) | (M4A_MIX_PC + 4);
  }

  while ((s32)r4 > 0 && samples < M4A_MIX_MAX_SAMPLES &&
         (samples == 0 || cycles < cycle_budget))
  {
    s32 sum;
    u32 product;
    u32 r5_off = r5 & 0x7FFF;
    u32 r7_off = r7 & 0x7FFF;

    sum = (s8)iwram[0x8000 + ((r5 + r6) & 0x7FFF)];
    sum += (s8)iwram[0x8000 + r5_off];
    sum += (s8)iwram[0x8000 + ((r7 + r6) & 0x7FFF)];
    sum += (s8)iwram[0x8000 + r7_off];
    r7++;

    product = (u32)sum * r3;
    r1 = product;
    r0 = (u32)((s32)product >> 9);
    if (r0 & 0x80)
      r0++;

    iwram[0x8000 + ((r5 + r6) & 0x7FFF)] = (u8)r0;
    iwram[0x8000 + r5_off] = (u8)r0;
    r5++;
    r4--;
    samples++;
    cycles += 29;
  }

  reg[0] = r0;
  reg[1] = r1;
  reg[4] = r4;
  reg[5] = r5;
  reg[7] = r7;
  next_pc = ((s32)r4 > 0) ? M4A_MIX_PC : 0x03002938U;
  return ((u64)cycles << 32) | next_pc;
}

#endif /* PD_M4A_HLE */
