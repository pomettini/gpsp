/* Phase-4 step 0: prove the device can execute freshly-emitted Thumb-2 from
 * a main-RAM buffer. v1 crashed: user code is UNPRIVILEGED, direct SCB cache
 * ops BusFault (crashlog: imprecise write to 0xE000EF68). v2 uses only
 * sanctioned APIs:
 *   - D-cache coherence WITHOUT SCB: capacity-evict by sweeping a 64KB
 *     buffer (H7 D-cache is 16KB), forcing the emitted lines out to RAM.
 *   - pd->system->clearICache() for the I-side (proven in vecx).
 * Tests:
 *   A: fill buf with BX LR, emit "MOVW r0,#0x1234; BX LR", evict, clear,
 *      call. OK=0x1234 -> PSRAM/.bss is executable, JIT viable.
 *   B: re-emit in place with #0x5678, NO eviction, clearICache only, call.
 *      0x5678 -> clearICache alone is coherent (does D-clean internally);
 *      0x1234 -> it is I-only and the dynarec needs an eviction strategy.
 *   C: emitted stub MOVW/MOVT-loads a C helper and BLX-calls it (+1).
 *      OK=0x43 -> emitted->C calls work (encoder seed for the emitter).
 * Progress is appended to jit_smoke.txt (open/write/close per step so it
 * survives a crash) plus the console. Build with make JITSMOKE=1. */

#ifdef PD_JIT_SMOKE

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "pd_api.h"

static uint8_t jit_buf[64] __attribute__((aligned(32)));
static uint8_t evict_buf[64 * 1024] __attribute__((aligned(32)));

static PlaydateAPI *spd;

/* Crash-surviving progress log: one open/write/close per step. */
static void step(const char *msg)
{
  SDFile *f = spd->file->open("jit_smoke.txt", kFileAppend);
  spd->system->logToConsole("jit-smoke: %s", msg);
  if (f)
  {
    spd->file->write(f, msg, (unsigned int)strlen(msg));
    spd->file->write(f, "\n", 1);
    spd->file->close(f);
  }
}

/* MOVW/MOVT encoders (Thumb-2 T3/T1): imm16 split as imm4:i:imm3:imm8. */
static void emit_mov16(uint16_t *out, int movt, unsigned rd, unsigned imm16)
{
  unsigned imm4 = (imm16 >> 12) & 0xF;
  unsigned i = (imm16 >> 11) & 1;
  unsigned imm3 = (imm16 >> 8) & 7;
  unsigned imm8 = imm16 & 0xFF;
  out[0] = (uint16_t)((movt ? 0xF2C0 : 0xF240) | (i << 10) | imm4);
  out[1] = (uint16_t)((imm3 << 12) | (rd << 8) | imm8);
}

/* Force the emitted lines out of the 16KB D-cache by capacity eviction:
 * volatile so the sweep is not optimized away. */
static void dcache_evict_sweep(void)
{
  volatile uint32_t *p = (volatile uint32_t *)evict_buf;
  uint32_t i;
  for (i = 0; i < sizeof(evict_buf) / 4; i++)
    p[i] = i;
}

static uint32_t helper_return_42(void)
{
  return 0x42;
}

void pd_jit_smoke_run(PlaydateAPI *pd)
{
  char buf[120];
  uint16_t *c = (uint16_t *)jit_buf;
  uint32_t (*fn)(void) = (uint32_t (*)(void))((uintptr_t)jit_buf | 1);
  uint32_t r;
  unsigned i;

  spd = pd;

  snprintf(buf, sizeof(buf), "v2 start, buf=%p helper=%p",
           (void *)jit_buf, (void *)helper_return_42);
  step(buf);

  /* Test A: safety-fill with BX LR, then MOVW r0,#0x1234; BX LR. */
  for (i = 0; i < sizeof(jit_buf) / 2; i++)
    c[i] = 0x4770; /* BX LR carpet: a stale/partial fetch returns early */
  emit_mov16(c, 0, 0, 0x1234);
  c[2] = 0x4770;
  step("A emitted; evicting D-cache");
  dcache_evict_sweep();
  step("A evicted; clearICache");
  pd->system->clearICache();
  step("A calling emitted code from main RAM");
  r = fn();
  snprintf(buf, sizeof(buf), "A=0x%x expect 0x1234 %s", (unsigned)r,
           r == 0x1234 ? "OK" : "FAIL");
  step(buf);

  /* Test B: re-emit in place (0x5678), NO eviction, clearICache only. */
  emit_mov16(c, 0, 0, 0x5678);
  step("B re-emitted; clearICache only (no eviction)");
  pd->system->clearICache();
  step("B calling");
  r = fn();
  snprintf(buf, sizeof(buf),
           "B=0x%x (0x5678: clearICache coherent; 0x1234: I-only)",
           (unsigned)r);
  step(buf);

  /* Test C: PUSH {lr}; MOVW/MOVT r1,=helper; BLX r1; ADDS r0,#1; POP {pc} */
  c[0] = 0xB500;
  emit_mov16(c + 1, 0, 1, (uint32_t)(uintptr_t)helper_return_42 & 0xFFFF);
  emit_mov16(c + 3, 1, 1,
             ((uint32_t)(uintptr_t)helper_return_42 >> 16) & 0xFFFF);
  c[5] = 0x4788; /* BLX r1 */
  c[6] = 0x3001; /* ADDS r0,#1 */
  c[7] = 0xBD00; /* POP {pc} */
  step("C emitted; evicting + clearICache");
  dcache_evict_sweep();
  pd->system->clearICache();
  step("C calling (emitted -> C helper -> back)");
  r = fn();
  snprintf(buf, sizeof(buf), "C=0x%x expect 0x43 %s", (unsigned)r,
           r == 0x43 ? "OK" : "FAIL");
  step(buf);

  step("done");
}

#endif /* PD_JIT_SMOKE */
