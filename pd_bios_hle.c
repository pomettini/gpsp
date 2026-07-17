/* Native (HLE) implementations of the hot BIOS copy/decompress SWIs:
 * CpuSet (0x0B), CpuFastSet (0x0C), LZ77UnCompWram (0x11) and
 * LZ77UnCompVram (0x12). The emulated open-BIOS loops run instruction by
 * instruction through the fetch-starved pipeline; these run as plain C on
 * reg[0..2], going through read/write_memory* so side effects (palette
 * conversion, SMC detection, mirrors) stay correct - SMC alerts trigger a
 * RAM-cache flush. Timing collapses to ~instant: the accepted accuracy
 * trade (NOTES.md). Enabled with BIOSHLE=1. */

#ifdef PD_BIOS_HLE

#include "common.h"

void pd_hle_bios(u32 num)
{
  u32 src = reg[0];
  u32 dst = reg[1];
  cpu_alert_type alerts = CPU_ALERT_NONE;

  switch (num)
  {
    case 0x0B: /* CpuSet */
    {
      u32 cnt = reg[2] & 0x1FFFFF;
      u32 fill = reg[2] & (1 << 24);
      if (reg[2] & (1 << 26))
      {                                   /* 32-bit units */
        u32 v = fill ? read_memory32(src) : 0;
        while (cnt--)
        {
          if (!fill)
          {
            v = read_memory32(src);
            src += 4;
          }
          alerts |= write_memory32(dst, v);
          dst += 4;
        }
      }
      else
      {                                   /* 16-bit units */
        u16 v = fill ? read_memory16(src) : 0;
        while (cnt--)
        {
          if (!fill)
          {
            v = read_memory16(src);
            src += 2;
          }
          alerts |= write_memory16(dst, v);
          dst += 2;
        }
      }
      reg[0] = src;
      reg[1] = dst;
      break;
    }
    case 0x0C: /* CpuFastSet: 32-bit units, count rounded up to 8 words */
    {
      u32 cnt = ((reg[2] & 0x1FFFFF) + 7) & ~7U;
      u32 fill = reg[2] & (1 << 24);
      u32 v = fill ? read_memory32(src) : 0;
      while (cnt--)
      {
        if (!fill)
        {
          v = read_memory32(src);
          src += 4;
        }
        alerts |= write_memory32(dst, v);
        dst += 4;
      }
      reg[0] = src;
      reg[1] = dst;
      break;
    }
    case 0x11: /* LZ77UnCompWram (byte writes) */
    {
      u32 remaining = read_memory32(src) >> 8;
      src += 4;
      while (remaining)
      {
        u32 flags = read_memory8(src++);
        u32 b;
        for (b = 0; b < 8 && remaining; b++, flags <<= 1)
        {
          if (flags & 0x80)
          {
            u32 hi = read_memory8(src++);
            u32 lo = read_memory8(src++);
            u32 len = (hi >> 4) + 3;
            u32 off = (((hi & 0xF) << 8) | lo) + 1;
            while (len-- && remaining)
            {
              alerts |= write_memory8(dst, read_memory8(dst - off));
              dst++;
              remaining--;
            }
          }
          else
          {
            alerts |= write_memory8(dst, read_memory8(src++));
            dst++;
            remaining--;
          }
        }
      }
      break;
    }
    case 0x12: /* LZ77UnCompVram: identical stream, 16-bit destination
                * writes (VRAM has no byte lanes). Buffer pairs. */
    {
      u32 remaining = read_memory32(src) >> 8;
      u32 pend = 0, pend_n = 0;
      src += 4;
      while (remaining)
      {
        u32 flags = read_memory8(src++);
        u32 b;
        for (b = 0; b < 8 && remaining; b++, flags <<= 1)
        {
          u32 len = 1, off = 0;
          if (flags & 0x80)
          {
            u32 hi = read_memory8(src++);
            u32 lo = read_memory8(src++);
            len = (hi >> 4) + 3;
            off = (((hi & 0xF) << 8) | lo) + 1;
          }
          while (len-- && remaining)
          {
            u32 c;
            if (off)
            {
              /* Back-reference against the LOGICAL output position
               * (dst + pend_n): off==1 with a pending byte references the
               * byte still buffered in `pend`; anything further back is in
               * already-flushed memory at dst + pend_n - off. */
              if (off <= pend_n)
                c = pend & 0xFF;
              else
              {
                u32 a = dst + pend_n - off;
                c = (read_memory16(a & ~1U) >> ((a & 1) * 8)) & 0xFF;
              }
            }
            else
              c = read_memory8(src++);

            pend |= c << (pend_n * 8);
            if (++pend_n == 2)
            {
              alerts |= write_memory16(dst, pend);
              dst += 2;
              pend = 0;
              pend_n = 0;
            }
            remaining--;
          }
        }
      }
      break;
    }
  }

#ifdef HAVE_DYNAREC
  if (alerts & CPU_ALERT_SMC)
    flush_translation_cache_ram();
#endif
}

#endif /* PD_BIOS_HLE */
