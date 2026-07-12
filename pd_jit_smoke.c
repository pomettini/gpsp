/* Phase-4 step 0: prove the device can execute freshly-emitted Thumb-2 from
 * a RAM buffer before any real dynarec work (see NOTES.md plan). Two tests:
 *   A: emit "MOVW r0,#0x1234; BX LR", expect 0x1234 back.
 *   B: emit a stub that MOVW/MOVT-loads a C helper's address and BLX-calls
 *      it, then adds 1 — verifies emitted->C calls and the encoder helpers
 *      that the real emitter will grow from. Expect 0x43.
 * Cache maintenance: clean D-cache by address (DCCMVAC) so the writes reach
 * memory, invalidate I-cache (ICIALLU), DSB/ISB. Each step logs to the
 * console BEFORE running so a crash pinpoints itself; results also go to
 * jit_smoke.txt in the Data folder. Build with make JITSMOKE=1. */

#ifdef PD_JIT_SMOKE

#include <stdint.h>
#include <string.h>

#include "pd_api.h"

static uint8_t jit_buf[64] __attribute__((aligned(32)));

/* MOVW/MOVT encoders (Thumb-2 T3/T1): imm16 split as imm4:i:imm3:imm8. */
static void emit_mov16(uint16_t *out, int movt, unsigned rd, unsigned imm16)
{
  unsigned imm4 = (imm16 >> 12) & 0xF;
  unsigned i = (imm16 >> 11) & 1;
  unsigned imm3 = (imm16 >> 8) & 7;
  unsigned imm8 = imm16 & 0xFF;
  out[0] = (movt ? 0xF2C0 : 0xF240) | (i << 10) | imm4;
  out[1] = (uint16_t)((imm3 << 12) | (rd << 8) | imm8);
}

#if defined(TARGET_PLAYDATE)
static void cache_flush_range(void *start, uint32_t size)
{
  volatile uint32_t *DCCMVAC = (volatile uint32_t *)0xE000EF68;
  volatile uint32_t *ICIALLU = (volatile uint32_t *)0xE000EF50;
  uint32_t a = (uint32_t)start & ~31u;
  uint32_t end = (uint32_t)start + size;

  __asm volatile("dsb" ::: "memory");
  for (; a < end; a += 32)
    *DCCMVAC = a;
  __asm volatile("dsb" ::: "memory");
  *ICIALLU = 0;
  __asm volatile("dsb" ::: "memory");
  __asm volatile("isb" ::: "memory");
}
#else
static void cache_flush_range(void *start, uint32_t size)
{
  (void)start; (void)size; /* host: nothing to do for this experiment */
}
#endif

static uint32_t helper_return_42(void)
{
  return 0x42;
}

void pd_jit_smoke_run(PlaydateAPI *pd)
{
  char report[256];
  int len = 0;
  uint16_t *c;
  uint32_t (*fn)(void);
  uint32_t r_a = 0, r_b = 0;

  pd->system->logToConsole("jit-smoke: buf=%p", (void *)jit_buf);

  /* Test A: MOVW r0,#0x1234; BX LR */
  c = (uint16_t *)jit_buf;
  emit_mov16(c, 0, 0, 0x1234);
  c[2] = 0x4770; /* BX LR */
  pd->system->logToConsole("jit-smoke: A emitted, flushing caches");
  cache_flush_range(jit_buf, sizeof(jit_buf));
  pd->system->clearICache();
  pd->system->logToConsole("jit-smoke: A calling");
  fn = (uint32_t (*)(void))((uintptr_t)jit_buf | 1);
  r_a = fn();
  pd->system->logToConsole("jit-smoke: A=0x%x (%s)", (unsigned)r_a,
                           r_a == 0x1234 ? "OK" : "FAIL");

  /* Test B: PUSH {lr}; MOVW/MOVT r1,=helper; BLX r1; ADDS r0,#1; POP {pc} */
  c = (uint16_t *)jit_buf;
  c[0] = 0xB500; /* PUSH {lr} */
  emit_mov16(c + 1, 0, 1, (uint32_t)(uintptr_t)helper_return_42 & 0xFFFF);
  emit_mov16(c + 3, 1, 1, ((uint32_t)(uintptr_t)helper_return_42 >> 16) & 0xFFFF);
  c[5] = 0x4788; /* BLX r1 */
  c[6] = 0x3001; /* ADDS r0,#1 */
  c[7] = 0xBD00; /* POP {pc} */
  pd->system->logToConsole("jit-smoke: B emitted, flushing caches");
  cache_flush_range(jit_buf, sizeof(jit_buf));
  pd->system->clearICache();
  pd->system->logToConsole("jit-smoke: B calling");
  fn = (uint32_t (*)(void))((uintptr_t)jit_buf | 1);
  r_b = fn();
  pd->system->logToConsole("jit-smoke: B=0x%x (%s)", (unsigned)r_b,
                           r_b == 0x43 ? "OK" : "FAIL");

  len = snprintf(report, sizeof(report),
                 "jit-smoke build %s %s\nbuf=%p\nA=0x%x expect 0x1234 %s\n"
                 "B=0x%x expect 0x43 %s\n",
                 __DATE__, __TIME__, (void *)jit_buf,
                 (unsigned)r_a, r_a == 0x1234 ? "OK" : "FAIL",
                 (unsigned)r_b, r_b == 0x43 ? "OK" : "FAIL");
  {
    SDFile *f = pd->file->open("jit_smoke.txt", kFileWrite);
    if (f)
    {
      pd->file->write(f, report, len);
      pd->file->close(f);
    }
  }
}

#endif /* PD_JIT_SMOKE */
