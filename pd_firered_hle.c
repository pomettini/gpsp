/* Native versions of four pure-memory routines and the dummy-OAM fill tail in
 * FireRed US 1.0's sprite pipeline. Entry blocks are enabled only when their
 * exact ROM bytes match. Callbacks and sprite submission remain in guest code.
 * Layout and behavior correspond to pret/pokefirered. */

#ifdef PD_FIRERED_SPRITE_HLE

#include "common.h"

#define FR_UPDATE_COORDS_PC  0x08006BF4U
#define FR_PRIORITIES_PC     0x08006CB8U
#define FR_SORT_PC           0x08006CF8U
#define FR_MATRICES_PC       0x08006EB8U
#define FR_OAM_FILL_PC        0x08006F40U
#define FR_OAM_FILL_NEXT_PC   0x08006F70U

#define FR_SPRITES_OFF       0x0002063CU
#define FR_PRIORITIES_OFF    0x00021780U
#define FR_ORDER_OFF         0x00021800U
#define FR_COORD_X_OFF       0x00021BC8U
#define FR_COORD_Y_OFF       0x00021BCAU
#define FR_MATRICES_OFF      0x00021BCCU
#define FR_OAM_LIMIT_OFF      0x00021B44U
#define FR_OAM_BASE_OFF      0x000030F0U

#define FR_DUMMY_OAM_ADDR     0x08231CE4U
#define FR_DUMMY_OAM_LO       0x013000A0U
#define FR_DUMMY_OAM_HI       0x00000C00U

#define FR_SPRITE_COUNT      64U
#define FR_SPRITE_SIZE       68U

u32 pd_firered_hle_matched;

static u32 fr_hash(u32 pc, u32 length)
{
  u32 hash = 2166136261U;
  u32 i;
  for (i = 0; i < length; i++)
    hash = (hash ^ read_memory8(pc + i)) * 16777619U;
  return hash;
}

int pd_firered_hle_matches(u32 pc)
{
  u32 length, expected;
  switch (pc)
  {
    case FR_UPDATE_COORDS_PC:
      length = 196; expected = 0x6751FB21U; break;
    case FR_PRIORITIES_PC:
      length = 64;  expected = 0xD2E19C2CU; break;
    case FR_SORT_PC:
      length = 448; expected = 0x75F48025U; break;
    case FR_MATRICES_PC:
      length = 76;  expected = 0x09E9CB87U; break;
    case FR_OAM_FILL_PC:
      length = 76;  expected = 0x3EAF1EA6U; break;
    default:
      return 0;
  }

  if (fr_hash(pc, length) != expected)
    return 0;
  if (pc == FR_OAM_FILL_PC &&
      (read_memory32(FR_DUMMY_OAM_ADDR) != FR_DUMMY_OAM_LO ||
       read_memory32(FR_DUMMY_OAM_ADDR + 4) != FR_DUMMY_OAM_HI))
    return 0;
  pd_firered_hle_matched = 1;
  return 1;
}

static u16 fr_ew16(u32 off)
{
  return readaddress16(ewram, off & 0x3FFFF);
}

static void fr_set_ew16(u32 off, u16 value)
{
  address16(ewram, off & 0x3FFFF) = value;
}

static u32 fr_ew32(u32 off)
{
  return readaddress32(ewram, off & 0x3FFFF);
}

static void fr_update_coords(void)
{
  u32 i;
  s32 global_x = (s16)fr_ew16(FR_COORD_X_OFF);
  s32 global_y = (s16)fr_ew16(FR_COORD_Y_OFF);

  for (i = 0; i < FR_SPRITE_COUNT; i++)
  {
    u32 off = FR_SPRITES_OFF + i * FR_SPRITE_SIZE;
    u32 flags = ewram[off + 0x3E];
    s32 x, y;
    u16 attr1;

    if ((flags & 5) != 1)
      continue;

    x = (s16)fr_ew16(off + 0x20) + (s16)fr_ew16(off + 0x24) +
        (s8)ewram[off + 0x28];
    y = (s16)fr_ew16(off + 0x22) + (s16)fr_ew16(off + 0x26) +
        (s8)ewram[off + 0x29];
    if (flags & 2)
    {
      x += global_x;
      y += global_y;
    }

    attr1 = fr_ew16(off + 2);
    fr_set_ew16(off + 2, (attr1 & 0xFE00U) | ((u32)x & 0x1FFU));
    ewram[off] = (u8)y;
  }
}

static void fr_build_priorities(void)
{
  u32 i;
  for (i = 0; i < FR_SPRITE_COUNT; i++)
  {
    u32 off = FR_SPRITES_OFF + i * FR_SPRITE_SIZE;
    u32 priority = ewram[off + 0x43] | ((ewram[off + 5] >> 2) & 3) << 8;
    fr_set_ew16(FR_PRIORITIES_OFF + i * 2, (u16)priority);
  }
}

static s32 fr_sprite_y(u32 index)
{
  u32 off = FR_SPRITES_OFF + index * FR_SPRITE_SIZE;
  u32 oam = fr_ew32(off);
  s32 y = (u8)oam;

  if (y >= 160)
    y -= 256;
  if ((oam & 0xC0000300U) == 0xC0000300U)
  {
    u32 shape = (oam >> 14) & 3;
    if ((shape == 0 || shape == 2) && y > 128)
      y -= 256;
  }
  return y;
}

static u32 fr_sort_sprites(void)
{
  u32 i, swaps = 0;
  for (i = 1; i < FR_SPRITE_COUNT; i++)
  {
    u32 j = i;
    while (j > 0)
    {
      u32 left = ewram[FR_ORDER_OFF + j - 1];
      u32 right = ewram[FR_ORDER_OFF + j];
      u32 left_pri = fr_ew16(FR_PRIORITIES_OFF + left * 2);
      u32 right_pri = fr_ew16(FR_PRIORITIES_OFF + right * 2);
      s32 left_y = fr_sprite_y(left);
      s32 right_y = fr_sprite_y(right);

      if (!(left_pri > right_pri ||
            (left_pri == right_pri && left_y < right_y)))
        break;
      ewram[FR_ORDER_OFF + j] = (u8)left;
      ewram[FR_ORDER_OFF + j - 1] = (u8)right;
      j--;
      swaps++;
    }
  }
  return swaps;
}

static void fr_copy_matrices(void)
{
  u32 i, lane;
  for (i = 0; i < 32; i++)
    for (lane = 0; lane < 4; lane++)
    {
      u16 value = fr_ew16(FR_MATRICES_OFF + i * 8 + lane * 2);
      u32 dst = FR_OAM_BASE_OFF + i * 32 + lane * 8 + 62;
      address16(iwram + 0x8000, dst & 0x7FFF) = value;
    }
}

static u32 fr_fill_oam(void)
{
  u32 sp = reg[REG_SP] & 0x7FFFU;
  u32 index = iwram[0x8000 + sp];
  u32 limit = ewram[FR_OAM_LIMIT_OFF];
  u32 count = 0;

  while (index < limit)
  {
    u32 dst = FR_OAM_BASE_OFF + 0x38U + index * 8U;
    address32(iwram + 0x8000, dst & 0x7FFFU) = FR_DUMMY_OAM_LO;
    address32(iwram + 0x8000, (dst + 4U) & 0x7FFFU) = FR_DUMMY_OAM_HI;
    index++;
    count++;
  }
  iwram[0x8000 + sp] = (u8)index;
  return count;
}

/* AAPCS u64 result: guest return PC in r0, approximate original cycles in r1. */
u64 pd_firered_hle(u32 pc)
{
  u32 cycles, next_pc = reg[REG_LR];
  switch (pc)
  {
    case FR_UPDATE_COORDS_PC:
      fr_update_coords();
      cycles = 1800;
      break;
    case FR_PRIORITIES_PC:
      fr_build_priorities();
      cycles = 1664;
      break;
    case FR_SORT_PC:
      cycles = 4096 + fr_sort_sprites() * 64;
      break;
    case FR_MATRICES_PC:
      fr_copy_matrices();
      cycles = 1280;
      break;
    case FR_OAM_FILL_PC:
      cycles = 64 + fr_fill_oam() * 32;
      next_pc = FR_OAM_FILL_NEXT_PC;
      break;
    default:
      cycles = 0;
      break;
  }
  return ((u64)cycles << 32) | next_pc;
}

#endif /* PD_FIRERED_SPRITE_HLE */
