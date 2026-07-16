/* TCM probe for the H7 Playdate (target=h7d1): maps which fast memory the
 * OS lets user code write AND execute, for relocating the dynarec's hot
 * memory handlers (vecx PLAYDATE_ITCM_GUIDE method, re-probed because that
 * guide's constants are for the F746 board; this device is different).
 *
 * Progressive stages, one per boot: the stage counter is persisted BEFORE
 * attempting the stage, so a crash (expected! crashed=1 on the next boot
 * banner) advances past the killer automatically. Relaunch until the log
 * says "probe done". Results accumulate in tcm_probe.txt.
 *
 * Stages:
 *  0 frame/sp info (safe)
 *  1 ITCM 0x00000100 write/readback
 *  2 ITCM 0x00000100 execute (copied MOVW/BX stub)
 *  3 ITCM 0x0000F000 write/readback (size ceiling)
 *  4 DTCM descending write-probe from frame-0x2180 (fault marks the floor)
 *  5 DTCM execute at the highest probed-good address
 *  6 done
 *
 * Build with make TCMPROBE=1. */

#ifdef PD_TCM_PROBE

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "pd_api.h"

static PlaydateAPI *spd;

static void plog(const char *msg)
{
  SDFile *f = spd->file->open("tcm_probe.txt", kFileAppend);
  spd->system->logToConsole("tcm-probe: %s", msg);
  if (f)
  {
    spd->file->write(f, msg, (unsigned int)strlen(msg));
    spd->file->write(f, "\n", 1);
    spd->file->close(f);
  }
  /* Console output is lost on a hard fault unless drained (vecx guide). */
  {
    uint32_t t = spd->system->getCurrentTimeMilliseconds();
    while (spd->system->getCurrentTimeMilliseconds() - t < 150u) {}
  }
}

static int read_stage(void)
{
  SDFile *f = spd->file->open("tcm_stage.txt", kFileReadData);
  char b[8] = {0};
  if (!f)
    return 0;
  spd->file->read(f, b, sizeof(b) - 1);
  spd->file->close(f);
  return b[0] ? (b[0] - '0') : 0;
}

static void write_stage(int s)
{
  SDFile *f = spd->file->open("tcm_stage.txt", kFileWrite);
  char b = (char)('0' + s);
  if (f)
  {
    spd->file->write(f, &b, 1);
    spd->file->close(f);
  }
}

/* MOVW r0,#0x1234; BX LR - the pd_jit_smoke stub, hardware-validated. */
static const uint16_t exec_stub[4] = { 0xF241, 0x2034, 0x4770, 0x4770 };

static int try_exec_at(uint32_t addr, char *buf, unsigned buflen)
{
  volatile uint16_t *dst = (volatile uint16_t *)addr;
  uint16_t saved[4];
  uint32_t (*fn)(void);
  uint32_t r;
  unsigned i;

  for (i = 0; i < 4; i++)
  {
    saved[i] = dst[i];
    dst[i] = exec_stub[i];
  }
  spd->system->clearICache();
  fn = (uint32_t (*)(void))(addr | 1);
  r = fn();
  for (i = 0; i < 4; i++)
    dst[i] = saved[i];               /* leave no trace (live vectors?) */
  spd->system->clearICache();
  snprintf(buf, buflen, "exec@%08x returned 0x%x %s", (unsigned)addr,
           (unsigned)r, r == 0x1234 ? "OK" : "BAD");
  return r == 0x1234;
}

void pd_tcm_probe_run(PlaydateAPI *pd)
{
  char msg[120];
  int stage = 0;

  spd = pd;
  stage = read_stage();

  while (stage <= 6)
  {
    write_stage(stage + 1); /* pre-advance: a crash skips this stage */

    switch (stage)
    {
      case 0:
      {
        void *frame = __builtin_frame_address(0);
        snprintf(msg, sizeof(msg), "stage0: frame=%p probe-build ready",
                 frame);
        plog(msg);
        break;
      }
      case 1:
      {
        volatile uint32_t *p = (volatile uint32_t *)0x00000100;
        uint32_t saved;
        plog("stage1: ITCM 0x100 write/readback...");
        saved = *p;
        *p = 0xCAFEBABE;
        snprintf(msg, sizeof(msg), "stage1: read back 0x%x %s",
                 (unsigned)*p, (*p == 0xCAFEBABE) ? "OK" : "BAD");
        *p = saved;
        plog(msg);
        break;
      }
      case 2:
      {
        plog("stage2: ITCM 0x100 execute...");
        try_exec_at(0x00000100, msg, sizeof(msg));
        plog(msg);
        break;
      }
      case 3:
      {
        volatile uint32_t *p = (volatile uint32_t *)0x0000F000;
        uint32_t saved;
        plog("stage3: ITCM 0xF000 write/readback...");
        saved = *p;
        *p = 0xDEADBEEF;
        snprintf(msg, sizeof(msg), "stage3: read back 0x%x %s",
                 (unsigned)*p, (*p == 0xDEADBEEF) ? "OK" : "BAD");
        *p = saved;
        plog(msg);
        break;
      }
      case 4:
      {
        uintptr_t frame = (uintptr_t)__builtin_frame_address(0);
        uintptr_t a = (frame - 0x2180) & ~(uintptr_t)63;
        uintptr_t floor_limit = a > 0x10000 ? a - 0x10000 : 0;
        plog("stage4: DTCM descending write-probe (fault = floor)...");
        for (; a > floor_limit; a -= 64)
        {
          volatile uint32_t *p = (volatile uint32_t *)a;
          uint32_t saved = *p;
          *p = 0x50524F42;
          if (*p != 0x50524F42)
            break;
          *p = saved;
          if ((a & 0x1FF) == 0)
          {
            snprintf(msg, sizeof(msg), "stage4: ok down to %08x",
                     (unsigned)a);
            plog(msg);
          }
        }
        snprintf(msg, sizeof(msg), "stage4: finished without fault at %08x",
                 (unsigned)a);
        plog(msg);
        break;
      }
      case 5:
      {
        uintptr_t frame = (uintptr_t)__builtin_frame_address(0);
        uintptr_t addr = (frame - 0x2180 - 64) & ~(uintptr_t)63;
        snprintf(msg, sizeof(msg), "stage5: DTCM execute at %08x...",
                 (unsigned)addr);
        plog(msg);
        try_exec_at(addr, msg, sizeof(msg));
        plog(msg);
        break;
      }
      case 6:
        plog("probe done");
        break;
    }
    stage++;
  }
}

#endif /* PD_TCM_PROBE */
