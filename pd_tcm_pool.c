/* DTCM relocation of the dynarec's memory-handler pool (H7 board).
 * The stub's handler region [__t2pool_start, __t2pool_end) is position-
 * independent (see thumb2_stub.S); this copies it into the probed DTCM
 * stack-shadow pool (NOTES.md "H7 fast-memory map": writable and
 * exec-proven just below frame-0x2180) and patches the ld/st dispatch
 * tables so ALL emitted code - including already-translated blocks -
 * dispatches into single-cycle TCM instead of I-cache-contended PSRAM.
 *
 * Every guest load/store goes through these handlers; emitted code churns
 * the 16KB I-cache, so keeping them resident is the vecx +15% playbook.
 *
 * Self-test gates the table patch: on failure the tables stay untouched
 * (correct, just slower). Build with make TCMPOOL=1 (device+DYNAREC only).
 * Must run AFTER init_emitter (reset_gba) refills the tables - call it
 * from start_emulation, every time. The copy itself happens once. */

#if defined(PD_TCM_POOL) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)

#include <stdint.h>

#include "pd_api.h"
#include "common.h"

extern char __t2pool_start[];
extern char __t2pool_end[];
extern void t2pool_selftest(void);

extern u32 ld_lookup_tables[5][17];
extern u32 st_lookup_tables[4][17];

static uintptr_t pool_base;  /* 0 until the copy+selftest passed */
static volatile u32 *pool_canary; /* word just above the pool code */

/* 1 = intact. Checked from the perf window so corruption is logged. */
int pd_tcm_pool_ok(void)
{
  if (!pool_base)
    return 1;
  return *pool_canary == 0xCA9A11E5;
}

static void patch_table(u32 *tbl, unsigned n)
{
  uintptr_t lo = (uintptr_t)__t2pool_start;
  uintptr_t hi = (uintptr_t)__t2pool_end;
  unsigned i;

  for (i = 0; i < n; i++)
  {
    uintptr_t e = tbl[i] & ~(uintptr_t)1;  /* entries carry the Thumb bit */
    if (e >= lo && e < hi)
      tbl[i] = (u32)(pool_base + (e - lo)) | 1;
  }
}

void pd_tcm_pool_install(PlaydateAPI *pd)
{
  uintptr_t size = (uintptr_t)__t2pool_end - (uintptr_t)__t2pool_start;

  if (!pool_base)
  {
    /* DTCM below the stack is firmware-OWNED on the H7 (writable but not
     * ours: three delayed-crash builds proved it - NOTES.md). Use the
     * malloc heap instead: it lives in on-chip AXI SRAM (0x24xxxxxx),
     * legally ours and far cheaper on I-cache miss than external PSRAM. */
    uintptr_t pool =
        ((uintptr_t)pd->system->realloc(NULL, size + 64) + 15) & ~(uintptr_t)15;
    const u32 *src = (const u32 *)__t2pool_start;
    volatile u32 *dst = (volatile u32 *)pool;       /* keep the manual copy */
    u32 words = (u32)(size / 4), i;
    u32 (*probe)(void);
    u32 r;

    for (i = 0; i < words; i++)
      dst[i] = src[i];
    /* Canary sits between the pool top and the stack margin: the stack
     * must kill it before it can reach the code. */
    pool_canary = (volatile u32 *)(pool + size);
    *pool_canary = 0xCA9A11E5;
    pd->system->clearICache();

    probe = (u32 (*)(void))((pool +
             (((uintptr_t)t2pool_selftest & ~(uintptr_t)1) -
              (uintptr_t)__t2pool_start)) | 1);
    r = probe();
    if (r != 0x7357)
    {
      pd->system->logToConsole("tcm-pool: selftest FAILED (0x%x), tables untouched",
                               (unsigned)r);
      return;
    }
    pool_base = pool;
    pd->system->logToConsole("tcm-pool: %u bytes at %p, selftest OK",
                             (unsigned)size, (void *)pool);
  }

#ifdef PD_TCM_POOL_NOPATCH
  /* Bisect build (TCMPOOL=2): install + selftest only, dispatch stays on
   * the PSRAM originals. */
  pd->system->logToConsole("tcm-pool: NOPATCH build, tables untouched");
#else
  patch_table(&ld_lookup_tables[0][0], 5 * 17);
  patch_table(&st_lookup_tables[0][0], 4 * 17);
#endif
}

#endif /* PD_TCM_POOL && HAVE_DYNAREC && TARGET_PLAYDATE */
