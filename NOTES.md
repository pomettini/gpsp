# gpSP → Playdate port notes

Working notes for the Playdate port. Updated every session.
Conventions follow my other ports (vecx, pokemini, nofrendo, red-viper):
C only, Playdate glue in `playdate_main.c` at repo root, diff against
upstream kept minimal, libretro build must keep working.

## Status

- **Phase 0 (survey): done** — this document.
- **Phase 1 (interpreter port): simulator milestone done** — Pokémon FireRed
  (16MB, exercises ROM paging past the 8MB cache) and homebrew boot and run
  in the simulator. Device binary links (232KB text / 18KB data / 1.20MB bss,
  matching the Phase-0 estimate) but is **not yet tested on hardware**.
  Audio is drained-and-discarded. Next: run on device, then wire audio.
- Phase 2 (renderer): fast path done — 64KB RGB565→luminance LUT, Bayer 4x4,
  8px→1byte packing (the 80px X offset is exactly byte 10), changed-row
  detection with `markUpdatedRows`. Cost not yet measured on device.
- Phase 3 (profiling): instrumentation in — per-update emu/blit ms accumulated
  and appended to `perf.log` on the Data partition every 600 updates; grab the
  file over the data disk. Frameskip implemented (System Menu: auto/off/1/2/3,
  default auto = skip while the previous update ran over 20ms, max 3 in a row);
  skipped frames bypass `update_scanline` and the blit entirely. Audio still
  drained/discarded. The "Press Select" menu slot was dropped for Frameskip —
  Select is crank-backward only now.
- **Phase 4 (Thumb-2 dynarec): WORKING ON DEVICE 2026-07-13** — FireRed
  2.48x over the interpreter (27.71ms vs 68.68ms, est 36 fps emulated),
  stable through scripted intro + manual play incl. a rival battle.
  DYNAREC=1 is now the default build; DYNAREC=0 keeps the interpreter
  for A/B. Remaining perf levers: -O3 on cpu_threaded, ITCM placement,
  translation-spike smoothing (worst=112ms).
- **Audio: wired (Phase 3 completion)** — pd_audio.c: main thread mixes
  (render_gbc_sound, the call the shell owns via PD_SHELL_AUDIO) and
  linear-resamples 65536->44100 Hz into a lock-free ring; the Playdate
  audio-thread callback only drains (vecx finding: never generate on the
  audio thread). Underruns emit silence: under-speed play gets gaps, not
  pitch shift. Cost shows as "aud" in perf.log. NOT yet heard on device.
- Phase 4 original gate notes (2026-07-12 smoke test):
  (build ad5b936). Facts established on the Rev B:
  - Main RAM (.bss, PSRAM 0x90xxxxxx) is executable from user code.
  - `pd->system->clearICache()` alone keeps emitted code coherent (test B:
    in-place re-emit + clearICache, no D-eviction, executed the NEW code) —
    the whole cache story is one call after emit.
  - Emitted→C calls via MOVW/MOVT+BLX work; encoders in pd_jit_smoke.c.
  - Direct SCB cache ops (0xE000EFxx) BusFault — user code is unprivileged.
    Never touch them; clearICache is the only sanctioned path.
  - Translation caches must be static buffers (no mmap): SMALL_TRANSLATION_
    CACHE (2MB ROM + 384KB RAM) fits — crashlog showed 9.9MB heap allocated,
    ~1.3MB bss, leaving ~2.5MB headroom on the 16MB budget.
  Next: model the backend on arm/arm_emit.h + arm_stub.S (ARMv6/7 is the
  closest register-pressure match), everything emits Thumb-2 (no ARM state;
  IT blocks or short branches replace conditional execution), function
  pointers carry the Thumb bit.

## H7 fast-memory map (TCM probe, 2026-07-16, OS 3.1.0, PDU4-Y017929)

- **ITCM 0x00000000: NO user access** - write, execute and high-offset write
  all fault. Closed.
- **DTCM: ~30KB clean pool** - writable from frame-0x2180 (~0x200079c0) down
  to 0x20000000 with NO firmware holes (F746 had holes; H7 has none). The
  stage-4 "crash at the end" was the probe descending below 0x20000000.
- **Execution from the pool: PROVEN** (exec@0x20007980 returned OK).
- init frame = 0x20009b50 (vs vecx F746 0x20009b88 - same neighborhood).
- Caveat: "writable" does not prove "unowned" - deep-bottom DTCM may hold OS
  data. Plan: place the pool in the stack shadow just below frame-0x2180
  (same placement class vecx proved), NOT at the DTCM bottom.
- Device firmware updated to 3.1.0 between sessions; probing was against
  3.1.0. Re-probe on OS updates (delete Data/tcm_stage.txt, TCMPROBE=1).

Relocation design (from these results): the stub's fast-path memory
handlers become position-independent (only reg_base-relative accesses;
slow-path exits via `ldr pc, =slow_label` literals, loader-relocated,
position-free) between __t2pool_start/__t2pool_end markers inside .text.
At init (DYNAREC device builds): manual word-copy into the DTCM pool,
clearICache, self-test, then PATCH ld/st_lookup_tables entries to the pool
copies (+Thumb bit). Emitted code reads handlers from the tables at runtime,
so already-translated blocks pick up the fast copies instantly; self-test
failure = leave tables untouched (correct, just slower). A/B via TCMPOOL=1.

## Bench 2026-07-17 77764ef RevB (heap handler pool, FireRed, bundled script)
avg_frame_ms=28.27 (control same-day: 30.09) | est fps=35.37 | no crashes

- Pool allocated at 0x90c6cb20: the malloc heap is PSRAM on this OS (no
  user-reachable AXI). The ~6% win is real anyway: the compact 2KB handler
  copy no longer shares I-cache sets with the churning translation caches.
- TCM experiment verdict: DTCM = firmware-owned (postmortem below), AXI =
  not user-reachable, heap pool = +6% and safe. Experiment closed; keep
  TCMPOOL=1. Next perf lever: inline EWRAM/IWRAM fastpaths in emitted code.

## Bench 2026-07-17 0d90074 RevB (inline load fastpaths, FireRed, bundled script)
avg_frame_ms=28.20 (pool-only: 28.27) | est fps=35.46 | stable

- FLAT. Inline page-map loads (5-inst fastpath, handler fallback) verified
  correct via host dump, but no measurable win over the heap pool: the
  emitted code itself streams from slow PSRAM, so ~20 extra bytes per load
  site cost about what the skipped handler round-trip saved.
- Conclusion: the fetch fabric (PSRAM + 16KB I-cache) is the wall, not the
  dispatch. INLINEMEM stays available but OFF by default (smaller emitted
  code). Future perf would need fundamentally less/hotter code per guest op
  (e.g. block-level optimizations in the translator), not micro-dispatch.

## Bench 2026-07-17 34d0b57 RevB - target-game calibration (bundled script)

Game                        | avg ms | est fps | rendered | worst
----------------------------|--------|---------|----------|------
Pokemon FireRed (intro)     | 28.20  | 35.5    | 30%      | 126
Wario Land 4                | 28.01  | 35.7    | 46%      | 64
Kirby Nightmare in Dream Land | 34.98 | 28.6   | 27%      | 254

- FireRed's intro is NOT the worst case: Kirby NiDL is heavier (late-era
  remake, blending effects, much more code - see worst=254 translation
  bursts). Wario renders the most frames (lightest PPU balance).
- All three sit at the plan's original success bar (~30fps emulated with
  frameskip). Full speed needs the 16-bit emission pass (CPU/fetch, 72% of
  frame time) + optionally a cheap-PPU mode (12ms/rendered frame; more
  rendered frames = visibly smoother, esp. Wario at 46%).

## Bench 2026-07-17 accd848 RevB (SCHEDBATCH, FireRed, bundled script)
avg=26.12ms (was 28.2) | est fps=38.28 | emu 21.7-23.2 | RENDERED 50% (was 30%)

- Scheduler calls collapsed 668 -> 225/frame exactly as designed; ~2.5ms
  emu win AND rendered-frame share jumped to 50% (big perceptual gain).
  FireRed correct: game plays, music tempo right (hblank-flag polling and
  cascade undercount not exercised). Audio choppiness unchanged
  (pre-existing under-speed gaps).
- SCHEDBATCH stays OPT-IN until validated on Wario/Kirby (hblank-flag
  polarity and cascade caveats in the commit) - then default it.
- Roadmap remainder to native (in order): 16-bit emission pass (emitted
  fetch), cheap-PPU mode (12ms/rendered frame), audio 32kHz, underclock
  knob, BIOS CpuSet/LZ77 HLE (Pokemon-heavy).

## Bench 2026-07-17 19e8ea9 RevB (NARROW 16-bit emission, FireRed)
avg=25.61ms (was 26.12) | est fps=39.05 | emu 20.9-21.0 | romtx 873KB (was 896)

- Real but modest: -0.5-0.8ms. Code shrank only 2.6% because emitted BYTES
  are dominated by memory-op sequences (MOVW/MOVT pc pairs, .W loads, USAT)
  that cannot narrow - ALU was a small share. Correctness held (roundtrip
  suite + gameplay).
- Next size lever therefore: literal pools for guest-PC constants (8B
  MOVW/MOVT -> 4B ldr + shared literal) rather than more ALU forms.
- Cumulative march: 68.7 -> 28.2 -> 26.1 -> 25.61ms; gap to native ~9ms.
  Remaining: cheap-PPU (biggest), pc-literal pools, audio 32kHz, underclock,
  BIOS CpuSet/LZ77 HLE. All flags still opt-in pending Wario/Kirby
  validation.

## Budget reconciliation (2026-07-18, lookup instrumentation, FireRed)

lookup=133/frame, gba est~12ms (incl ~2ms getElapsedTime overhead -> ~10ms
real). KEY REALIZATION: "gba est" samples the WHOLE update_gba, and
update_scanline (PPU) is called INSIDE update_gba - so the scheduler figure
already contains PPU render. Corrected budget of the 22.3ms emu:
  - update_gba bundle (scheduler + PPU render): ~10ms real
  - emitted-code execution + 133 C block-lookups + handlers: ~12ms
Native target 16.7ms; frame avg 26.5 (emu 22.3 + aud 1.8 + blit 2.9).

## Native-speed campaign, round 1 (2026-07-20)

Executed against the recorded plan; all numbers FireRed on device.

- **Scheduler round 3 (SCHEDBATCH2, kept)**: scanline fast-forward — jump
  runs of idle lines in one event, stopping at the VCOUNT match line
  (FireRed arms VCOUNT IRQ @150 permanently; hblank IRQ/HDMA unarmed),
  vblank start (160) and frame wrap (228). Vdraw coalesces on skipped
  frames only; vblank on every frame. Jumps capped at 24 lines: unbounded
  jumps batched ~100 sound-timer periods per event and produced audible
  ticking (user-verified gone at cap 24, cost ~0.7ms vs uncapped).
  Intro bench 26.12 -> 24.77ms (est 40.4 fps), gba 227 -> 88/upd.
- **Lookup front cache (LOOKUPCACHE, dead end)**: warm block lookup
  measured at 0.21-0.39us/call (boot ubench, logged at startup); 150-340
  lookups/frame is at most ~0.3ms. Front cache flat (24.78 vs 24.77).
  Flag kept, out of the shipping stack. Lookups are NOT a bottleneck.
- **perf.log now splits emuR/emuS (rendered/skipped emu ms) and logs
  gfps** (true emulated guest-frame rate via frame_counter; native=59.73).
  Caveat: catch-up updates run 2 guest frames and charge both to that
  update's bucket, so emuR overstates rendered-frame cost when pacing.
- **Overworld reality check**: intro bench is NOT representative.
  Overworld (walking, scripted bench below): gfps 36-37 vs intro 40-44,
  emuS ~20ms vs ~13ms, lookups ~344/upd, romtx grows to 1.86MB (near the
  2MB SMALL_TRANSLATION_CACHE limit -> flush storms loom in later areas).
  Pure CPU cost per overworld frame ~20ms is the wall between here and
  native; PPU render is only ~2-2.5ms (emuR-emuS after double-frame
  correction, consistent with the old PPUOFF measurement).
- **Overworld bench**: Source/bench_firered_overworld.txt (also pushed to
  /Shared/Emulation/gba/bench_script.txt override) — waits out intro,
  continues the save (3x A), paces LEFT/RIGHT/DOWN ~100s. UP token is the
  Start bridge, so the walk never uses UP; no A during walking (dialog
  risk), B taps dismiss strays. Baseline: est 36.71 fps, avg 27.24ms.
- **SAVE-WIPE POSTMORTEM**: user's FireRed save was destroyed at 01:17 —
  the game issued a flash chip erase (gba_memory.c handles cmd 0x10 with
  a full-buffer 0xFF memset) during the bench marathon and the shell
  persisted the erased image on exit. Restored from the user's manual
  test.sav.bak (valid, 28 section magics); local backup kept at
  ~/gpsp-saves/firered-20260714.sav. Shell now REFUSES to overwrite an
  existing save file when the backup buffer is all-0xFF (logs instead).
- **Blit LUT**: light half of the contrast curve reverted to linear —
  highlight compression made light-gray text invisible on white windows
  (FireRed save screen). Dark-half square curve (the readability fix)
  kept.

Round-1 verdicts (2026-07-20, overworld bench):
- ROM translation cache 2MB -> 3MB: flat on the bench as expected
  (thrash insurance; overworld alone was at 93% of 2MB). Kept.
- LITPOOL (PC-literal pools): pools engaged fully (romtx 1860 -> 1489KB,
  -20%) but emuS got ~2ms WORSE (21.5-22.8 vs 19.5-20.6). Each block's
  pool read adds D-side PSRAM traffic, and a D-miss costs more than the
  fetches saved. Flag kept, OUT of the stack. Together with INLINEMEM
  (flat) this establishes: the M7 here is PSRAM-LATENCY-bound, not
  fetch-bandwidth-bound - never trade instruction bytes for data reads.

## Emitted-code audit + COMPACTMEM (2026-07-20)

Corpus: device translation cache after the overworld bench (DUMPJIT=1 ->
Data/jitrom.bin + jithash.bin + jitram.bin; analysis in the session log).
5778 blocks, 419k insts, 1.5MB. Findings: ~55% of all emitted bytes were
per-site memory-op scaffolding (inline fastpath + fallback + region
dispatch + PC pair); peephole-level fat (mov pairs, adds #0, dead cmp)
only ~2%. Idle-loop elimination confirmed WORKING (vblank wait's backward
branch calls update_gba_idle).

COMPACTMEM (kept): all 9 region-dispatch sequences moved into shared
stub dispatchers (arm/thumb2_stub.S mem_dispatch_builder); sites shrink
to movw/movt(pc)+BL. Same-workload romtx 1860 -> 996KB (-46%). emuS
19.5-20.6 -> 19.0-19.2ms, bench flat. Byte-identical dispatch verified
against the corpus disassembly. INLINEMEM dropped from the stack (its
inline fastpaths are superseded; flag remains).

**THE WALL, quantified**: halving fetch volume bought <1ms, inline loads
were flat, literal pools (adding D-traffic) LOST 2ms. Together: the
overworld's ~19ms pure-CPU frame is dominated by per-operation PSRAM
LATENCY - mostly D-cache misses on guest EWRAM/VRAM/OAM (~400KB working
set vs 16KB D-cache, ~265 cycles a miss) plus I-misses on block entry.
No emitter restructuring changes that; DTCM/ITCM are closed (see TCM
postmortem). Native 59.7 gfps for FireRed OVERWORLD is out of reach on
this hardware by roughly 2x; the honest target is ~40 gfps overworld /
~48 intro via trims:
- blit u32 rewrite (3.3ms -> target ~1.7ms)
- SOUND32K relanding with a PSG low-pass for the aliasing (-0.85ms)
- audio-call batching, pacing polish
Wario/Kirby (lighter engines) should be re-benched on the final stack -
they may land near-native with frameskip.

## SOUND32K resolution + shipping defaults (2026-07-22)

- Rate audit found the integer-only conversion had left four 65.536kHz
  assumptions behind: square/wave/noise register writes and the 256Hz PSG
  sequencer. The earlier repair covered only square-channel sweep updates.
  All PSG steps now share one compile-time mix-rate scale; the 64k path is
  bit-identical and the 32k path advances at twice the per-sample step.
  DirectSound timing and the 32.768->44.1kHz resampler were already correct.
- RevB listening verdict: button/text-advance effects remain a little
  high/annoying, but are acceptable for the speed benefit. The one-pole
  low-pass stays and SOUND32K is KEPT; SOUND32K=0 remains the clean-audio
  fallback if the tradeoff needs revisiting.
- Overworld playbench, final candidate: 4545 frames, avg 26.60ms, worst
  118ms, est 37.59 fps. Settled perf.log windows: aud 1.09-1.14ms, emu
  23.93-24.02ms, blit 3.11-3.23ms, gfps 37.0-37.4, romtx 995-1000KB.
  This confirms the earlier ~1.1ms SOUND32K audio cost (vs ~1.9ms at 64k);
  it is not a new controlled A/B measurement.
- Bare `make` now selects the accepted shipping profile: DYNAREC,
  TCMPOOL, SOUND32K, BIOSHLE, NARROW, SCHEDBATCH, SCHEDBATCH2 and
  COMPACTMEM. Each remains individually disableable with `FLAG=0`.
  BENCH, SCHEDSTATS and all diagnostic/failed experiment flags stay opt-in.

## CPU underclock experiment: KILLED (2026-07-22)

- Tier 2 kept video, timer and audio clocks normal while giving the
  dynarec an 8/9 CPU-cycle budget (+12.5% charged cycles).
- On the same 4545-frame overworld script, the baseline was 26.60ms avg,
  37.59 estimated fps and 118ms worst; tier 2 measured 26.64ms avg, 37.53
  estimated fps and 126ms worst. This is flat, with a slightly worse tail.
  Matching perf.log windows during the script were also unchanged. Later
  faster-looking windows occurred after the scripted report and are not a
  controlled comparison.
- Pokemon's idle-loop elimination already advances idle guest cycles at
  very low host cost. Charging more per active dynarec cycle therefore
  removes cheap idle budget without reducing the fixed PSRAM-bound active
  work; stronger tiers would instead risk missing guest deadlines.
- The implementation was removed and the normal CPU budget restored.
  This experiment is closed.

## PLAN OF ATTACK TO NATIVE (ranked by measured headroom):
1. Scheduler round 2 (~10ms bundle, biggest): batch is at 227 calls/frame.
   Push further - lazy VCOUNT so vdraw scanlines coalesce when no per-line
   IRQ/DMA/hblank flag is armed (Pokemon overworld: none). Target <120
   calls. Also: the PPU render inside it is the real pixel cost - a
   dirty-region skip (only re-render changed BG/OBJ scanlines) is possible.
2. Inline PC->block cache (133 lookups/frame): direct-mapped [hash]->{pc,ptr}
   checked in-stub before the C fallback. ~1-2ms.
3. PC-literal pools (emitted size -> fetch): ~10-15% code, the general lever.
4. Trims: underclock knob, audio call batching.
Honest ceiling: even stacked, FireRed to true 16.7ms is not guaranteed on
this fetch fabric; likeliest outcome is ~20ms (50 fps emulated, ~all frames
rendered = feels native) with Wario/Kirby close behind. Full 59.7 native
may require the block-linking of indirect branches (#2) to land big.

## Session 2026-07-18: BIOS HLE + render/audio quality passes

- BIOS HLE (BIOSHLE=1) shipped: CpuSet/CpuFastSet/LZ77 native. Bench-flat
  as expected (intro is dialog); the win is in door/battle transitions.
  Shipped with an LZ77-VRAM back-reference bug (logical position math) that
  corrupted decompressed fonts/sprites - fixed same day. LESSON: HLE
  decompressors get host-side unit tests against reference data BEFORE the
  flag defaults on.
- Contrast S-curve in the luminance LUT: linear mapping had dithered text
  and mid-dark backgrounds into competing checkers since Phase 2; squaring
  toward the extremes made dialogue readable (user-verified).
- SOUND32K tried (-0.85ms, 25.19ms avg / 39.7 est fps): PSG pitch fixed
  (frequency step now derives from the mix rate - bit-identical at 64k),
  but residual square-wave aliasing stays audibly harsh. DROPPED from the
  shipping stack: 0.85ms is not worth twice-flagged audio. Flag remains.
- Shipping stack: BIOSHLE + NARROW + SCHEDBATCH + TCMPOOL + INLINEMEM at
  ~26ms / ~38.4 est fps / ~50% rendered. Remaining levers: PC-literal
  pools, then Wario/Kirby validation sweep before defaulting the stack.

## PPUHALF experiment: KILLED (2026-07-17, FireRed)

avg=25.67 (flat vs 25.61) AND a real artifact: FireRed's intro portraits
use the interlaced-object trick (large sprites drawn on alternating
scanlines), which line-duplication visibly breaks (double-height Oak).
Flat + ugly = dead. Important correction: the "12ms per rendered frame"
PPU attribution from the PPUOFF run was evidently NOT pure scanline work
(second-order effects); halving scanline rendering bought ~nothing.
PPUHALF flag remains for reference but should not be shipped.

Honest revised roadmap to native (~9ms gap): audio 32kHz (~1ms),
PC-literal pools (emitted size), BIOS CpuSet/LZ77 HLE (Pokemon loads),
then diminishing returns - full native on FireRed may not be reachable
on this fetch fabric; lighter games remain the best candidates.

## Scheduler cost measurement (2026-07-17, SCHEDSTATS, FireRed)

perf.log: **gba=666-669 calls per guest frame, 18-19us sampled per call,
est 12.0-12.6ms/frame** (sample includes some getElapsedTime overhead, so
true total is 6-12ms - either way the dominant single cost in the 20.3ms
CPU budget). Breakdown: 456 video events (2 per scanline: hblank + line
advance) + ~212 sound-timer fires. THE BOTTLENECK IS SCHEDULER ROUND-TRIPS,
not emitted-code fetch. Plan (PD_SCHED_BATCH):
1. One event per scanline: set the hblank flag/IRQ/DMA at the line-advance
   event (timing skew <=272 cycles; flag polling coarsens - acceptable).
2. Batch sound-timer fires: loop the overflow logic (N periods per call)
   and stop capping execute_cycles for non-IRQ timers.
Expected: 668 -> ~260 calls/frame, saving 4-8ms.

## Frame-time decomposition (2026-07-17, PPUOFF measurement, FireRed)

PPUOFF=1 run (100% skip): emu avg=20.3ms. Against the inline build's
steady state (emu 24.5ms at ~35% rendered):

- pure CPU (emitted code + update_gba + handlers): 20.3ms (72%)
- PPU: ~12ms PER RENDERED FRAME (4.2ms amortized at 35% rendered)
- audio 1.9ms, blit ~1ms amortized

Conclusions: CPU/fetch remains lever #1 (16-bit emission pass); the PPU is
a fat #2 (12ms/rendered frame - a cheap-render mode would let MORE frames
render, not just save time). Levers ranked in the "native performance"
discussion of this date.

## DTCM ownership postmortem (2026-07-17, three crash builds)

**DTCM below the stack frame is firmware-OWNED on the H7 despite being
writable and executable.** The probe (write+restore, traceless) and the
exec test (also restoring) both "pass" there - but the pool install leaves
code resident, corrupting live OS state; the OS then crashes LATER at a
deterministic wild PC (0x24021036, AXI region), identically across pool
layouts and margins (0x2180 and 0x4000) and even with dispatch tables
unpatched (TCMPOOL=2 bisect). Ownership cannot be detected by traceless
probing - only leave-corrupted-and-observe cycles would map the true gap,
if one exists on this OS at all.

Resolution: the handler pool now lives in the MALLOC HEAP, which the
crash registers show is served from on-chip AXI SRAM (0x24xxxxxx) - legally
owned, cached (not TCM), but far cheaper on I-cache miss than external
PSRAM. The PIC handler region + table patching machinery is unchanged.
Control build also confirmed: OS 3.1.0 runs the plain dynarec fine
(30.09ms, caches in PSRAM as always).

## Phase 4 workplan (from the study pass of the ARM backend)

Facts about the existing backend that shape the port:

- `cpu_threaded.c` selects the backend by arch define (`ARM_ARCH` →
  `arm/arm_emit.h`); only ~7 `generate_*` entry points cross the boundary —
  the backend's ~364 macros are self-contained. New define: `THUMB2_ARCH` →
  `arm/thumb2_emit.h` (one small upstream diff in the include ladder).
- `arm_stub.S` also DEFINES the guest arrays (ewram/iwram/vram/reg/...) and
  the translation caches as static .bss (`defsymbl`) — no mmap anywhere on
  the ARM path. The Thumb-2 stub keeps that layout; `memmap.c` stays unused.
- Register conventions port 1:1 to Thumb-2 (all are r0-r14 usable in T32):
  r3-r8 = allocatable x0-x5, r0-r2 = args/temps, r9 = flags/scratch0,
  r11 = reg_base, r12 = cycles, r14 = rs temp.
- `platform_cache_sync(base, end)` is the per-emit cache hook → calls
  `pd->system->clearICache()` via an extern function pointer set by the
  shell (proven coherent by smoke test B; cpu_threaded.c cannot include
  pd_api.h).
- Branch reach: T32 B.W/BL ±16MB, conditional B.W ±1MB — translation caches
  are 2MB+384KB adjacent in .bss, all in range. `write32(value)` emits ARM
  words; T32 needs `write16` pairs, first halfword first.

M-profile deltas found reading arm_stub.S (they change the emitted ABI):

- No CPSR on Cortex-M: `mrs reg_flags, cpsr` / `msr cpsr_f, reg_flags`
  become `mrs rX, apsr` / `msr apsr_nzcvq, rX`.
- `return_add()` (`add pc, lr, #4`, skipping a PC-literal placed after the
  emitted BL) does not port: T32 forbids that PC write and LR carries the
  Thumb bit. Deviation: the Thumb-2 emitter passes the guest PC in r0 via
  MOVW/MOVT *before* the BL (no inline literal), stubs return with plain
  `bx lr`. +4 bytes per call site, zero LR arithmetic.
- `stmne`/conditional sequences → IT/ITT blocks (store_registers_cond etc.).
- `extract_u16` → `uxth` (fine on M7).

Porting checklist (order of work):

Static-audit findings (fixed before first execution):

- **Interworking**: `block_lookup_address_*` returns raw even cache
  addresses; every stub `bx r0` block entry now ORRs the Thumb bit first
  (even `bx` target on M7 = HardFault). 17 sites.
- Condition 0x0F (opposite-of-AL) can never reach the branch filler: the
  frontend skips the conditional header for AL and Thumb has no AL cond
  branch — no "never" branch encoding needed.
- The frontend has zero direct references to backend encodings (verified);
  `bios_swi_entrypoint` only flows through the branch patcher, which masks
  bit0.

Host translation-dump harness (pd_dynarec_hoststub.c + PD_TRANSLATE_DUMP
in the shell, simulator+DYNAREC build): the translator is pure C, so the
host build translates real guest blocks and dumps the emitted Thumb-2 to
Data/jitdump.bin for offline disassembly - emitted code is NEVER executed
off-device (dynarec_enable stays 0; this is a translator check, not
emulator testing). Verified against real Tetris-homebrew blocks: register
allocation, condition inversion, cycle accounting, handler-table offsets,
CBNZ skip distances and block-link patching all correct, instruction by
instruction.

Progress: encoders DONE (GAS-roundtrip tested, tests/thumb2gen.c),
stub DONE (assembles, disasm-verified, includes SDIV-based SWI 6/7 HLE),
emitter DONE (compiles+links under DYNAREC=1: 478KB text / 4.08MB bss with
the small caches), shell wired (dynarec_enable=1 in DYNAREC builds,
clearICache hook). NOT yet executed on device — runtime bring-up next:
homebrew first, then FireRed, then the bench script, expecting >2x.
Multiply-long S-forms approximate N (lo31|hi31) — revisit only if a game
misbehaves.

1. `arm/thumb2_codegen.h` — T32 encoder primitives (write16-based):
   MOVW/MOVT (validated in pd_jit_smoke.c), data-proc reg/imm forms
   (modified-immediate encoding!), LDR/STR imm/reg, LDM/STM (T32 limits:
   no writeback+base-in-list), B.W/BL/B<cond>.W fixups, IT.
2. `arm/thumb2_stub.S` — port of arm_stub.S in `.syntax unified` `.thumb`:
   every `defsymbl` code symbol needs `.thumb_func`; ARM conditional
   sequences → IT blocks; keep the data section verbatim.
3. `arm/thumb2_emit.h` — port of arm_emit.h on top of (1): conditional
   emission macros wrap an IT or a short branch-around; ARM's
   `arm_relative_offset` (imm24, -8 pipeline) → T32 branch encoder (-4).
4. Makefile `DYNAREC=1`: `-DHAVE_DYNAREC -DTHUMB2_ARCH`, adds
   cpu_threaded.c + thumb2_stub.S. Default OFF until bring-up completes.
5. Bring-up ladder (each step measured with the bench script):
   a. DYNAREC=1 build compiles, `dynarec_enable=0` → behaves as today.
   b. Homebrew tetris boots with dynarec on.
   c. FireRed boots and plays.
   d. Bench: expect >2x on avg_frame_ms; record in the bench table.
6. After correctness: idle-loop elimination (gba_over already provides
   per-game `idle_loop_target_pc` — dynarec-only feature, big Pokémon win),
   then ITCM placement experiments per the vecx guide.

## Phase 1 — decisions and findings

- **Builds**: root `Makefile` → `gpsp.pdx` (device+simulator);
  libretro build renamed to `Makefile.libretro` (verified still builds:
  `make -f Makefile.libretro`). `cpu.cc`/`video.cc` are templated C++ and
  compile with `arm-none-eabi-g++ -fno-exceptions -fno-rtti
  -fno-threadsafe-statics`; everything new is C.
- **Shell files**: `playdate_main.c` (lifecycle, input callback, save RAM,
  frame loop), `render.c` (blit), `pd_filestream.c` (libretro VFS API on
  `pd->file`, core untouched), `pd_syscalls.c` (newlib stubs; `_gettimeofday`
  maps the Playdate clock +946684800 so the emulated RTC works),
  `rom_picker_unit.c` (pd-rom-picker submodule glue).
- **BIOS**: always the embedded open BIOS — the shell MUST
  `memcpy(bios_rom, open_gba_bios_rom, 16KB)` before `reset_gba()`.
  Forgetting it = guest falls into open-bus, whose handler recurses
  `read_memory32` until stack overflow (found the hard way with FireRed;
  homebrew that never SWIs boots fine without it).
- **Frame pacing (interim)**: display at 50Hz, exactly one emulated frame
  per update callback → ~84% speed (50/59.73). Correctness-first choice;
  Phase 3 revisits (options: catch-up second frame every ~6 updates, or
  frameskip-driven). Audio is discarded so pitch is moot for now.
- **Start/Select**: crank flick forward ≥45° = Start (8-frame press), flick
  backward = Select; plus System Menu items "Press Start"/"Press Select".
  L/R still unmapped (crank-hold proposal stands, revisit when a game
  needs it).
- **Save RAM**: whole 128KB `gamepak_backup` written as `<rom>.sav` to the
  Data folder on terminate/lock/low-power/back-to-picker.
- **ROMs**: picker scans `/Shared/Emulation/gba/games/` (`.gba`/`.agb`);
  a bundled `Source/test.gba` (git-ignored) boots directly for dev.
- **Simulator gotchas**: the sim build must be `-O2` (interpreter's
  macro-expanded frames overflow the sim thread stack at `-O0`); SDK
  `common.mk` expands rule prerequisites at include time, so extra objects
  ride in via `ULIBS` + an added prerequisite line; never pass `ASFLAGS` to
  a `.S` rule (its `-Wa,-amhls=$(<:.s=.lst)` doesn't substitute capital `.S`
  and overwrites the source with the listing).
- **Headless verification**: `TARGET_SIMULATOR` builds dump raw RGB565
  frames (100/300/600) to the Data folder; convert offline to inspect.
  Simulator also echoes `logToConsole` to stdout when launched from a
  terminal.

---

## Phase 0 — codebase survey

Fork point: davidgfnet/gpsp lineage, v1.1.0 (`gpsp_config.h`).

### Core (platform-independent, compile on Playdate)

| File | Role |
|---|---|
| `cpu.cc` | ARM7TDMI interpreter (`execute_arm(cycles)` runs one frame). Also *defines* the guest RAM arrays (ewram/iwram/vram/palette/oam/io). C-compatible despite `.cc` extension (upstream quirk; builds as C-ish C++ — verify it compiles as C or keep g++ for this TU, see Phase 1 risks). |
| `gba_memory.c` | Memory map, ROM loading + 32KB-page LRU cache, backup (SRAM/Flash/EEPROM), DMA, GPIO (RTC/rumble), game-specific overrides via `#include "gba_over.h"`. |
| `video.cc` | PPU renderer. `update_scanline()` called per scanline from `update_gba()`, writes 15-bit BGR `u16` pixels into `gba_screen_pixels` (pitch 240, shell-allocated). |
| `sound.c` | PSG + DirectSound mixing into a static ring buffer. Shell pulls with `sound_read_samples(s16*, frames)`. Mix rate `GBA_SOUND_FREQUENCY = 65536 Hz` stereo (fixed const). |
| `main.c` | Scheduler: `update_gba()` (timers, video events, DMA, IRQs), `reset_gba()`, `init_main()`. |
| `input.c` | GBA keypad logic + turbo. Reads input through a `retro_input_state_t` **callback** set via `retro_set_input_state()` — the Playdate shell can provide its own callback, no core change needed. |
| `savestate.c` | BSON savestates, in-memory (shell does the file I/O). |
| `cheats.c` | Gameshark/CB. Small; compile, no UI initially. |
| `serial.c`, `gbp.c` | SIO plumbing + Game Boy Player handshake. Called from `update_gba()`/`update_input()` — compile them (small), run with serial disabled. |
| `rfu.c`, `serial_proto.c` | Wireless adapter + netplay serial. Networked multiplayer — irrelevant on Playdate; try to exclude, stub if `serial.c` requires symbols. |
| `bios_data.S` | Embedded open-source BIOS (16KB `.incbin`, `open_gba_bios_rom`). Means no BIOS file dependency on Playdate. |
| `gbp.c`, `memmap.c` | `memmap.c` is mmap wrapper for the JIT cache only — not compiled in interpreter build (and useless on device; Phase 4 uses a static buffer instead). |
| `gba_cc_lut.c` | 128KB color-correction LUT used only by the libretro shell's post-processing. **Exclude from Playdate build** (we go to 1-bit anyway). |

### Shell (libretro — replaced by `playdate_main.c`)

`libretro/libretro.c` + `libretro/libretro-common/*`. Responsibilities the
Playdate shell must reimplement:

- Allocate `gba_screen_pixels` (240×161×2 = 77,280 B; note the +1 row slack upstream allocates).
- Frame loop: `update_input()` → `clear_gamepak_stickybits()` + `execute_arm(execute_cycles)` (one frame) → pull audio → present video. Frameskip logic lives here too.
- Call once at boot: `init_gamepak_buffer()`, `init_sound()`, `load_gamepak(...)`, `reset_gba()`.
- Persist `gamepak_backup[]` (SRAM/Flash/EEPROM, ≤128KB) to disk; upstream writes it on unload/periodically.
- BIOS selection (we always use the embedded open BIOS).

### Core → libretro dependencies to handle

1. **`libretro.h` types**: core headers include it for enums/typedefs only
   (`retro_game_info`, `retro_input_state_t`). Header-only — keep including
   it on Playdate, no runtime needed.
2. **VFS**: `gba_memory.c` uses `filestream_*` (open/read/seek/close/get_size)
   from libretro-common for ROM paging. libretro-common's implementation sits
   on stdio, which doesn't exist on the device. Plan: small
   `file_stream.h`-compatible shim backed by `pd->file` (SDFile), so
   `gba_memory.c` stays untouched. (~6 functions.)
3. **`load_gamepak(const struct retro_game_info*, ...)`**: pass `info = NULL`
   and a path, which takes the paging path (info-with-data path would need the
   whole ROM in memory — we don't want that).

### ROM paging (already suits the Playdate)

- Backing store: up to 32 × 1MB `malloc`ed blocks (`gamepak_buffers`),
  count set by `ROM_BUFFER_SIZE` (compile-time, in MB; default 32).
  `init_gamepak_buffer()` gracefully stops at malloc failure → allocate-what-fits.
- Page granularity 32KB, LRU eviction (`evict_gamepak_page`), sticky bits
  protect pages touched in the current frame (interpreter path).
- ROM ≤ cache size → fully resident, no paging I/O after load.
- Plan: `-DROM_BUFFER_SIZE=8` initially (8MB cache; most mode-0 titles are
  4–8MB, Wario Land 4 is 8MB). Tune from the budget after measuring.

### Allocation inventory

Static (`.bss`/`.data`), interpreter build:

| Symbol | Where | Size |
|---|---|---|
| `ewram[256K*2]` | cpu.cc | 512 KB (upper 256KB = SMC tag mirror; read unconditionally by write paths — halving it means patching addressing macros, not Phase-1 material) |
| `iwram[32K*2]` | cpu.cc | 64 KB (same mirror scheme) |
| `vram[96K]` | cpu.cc | 96 KB |
| `palette_ram`, `palette_ram_converted`, `oam_ram`, `io_registers` (512×u16 each) | cpu.cc | 4 KB |
| `bios_rom[16K]` | gba_memory.c | 16 KB |
| `open_gba_bios_rom[16K]` | bios_data.S (.data) | 16 KB |
| `gamepak_backup[128K]` | gba_memory.c | 128 KB |
| `memory_map_read[8192]` ptrs | gba_memory.c | 32 KB (32-bit ptrs) |
| `sound_buffer[65536]` s16 | sound.c | 128 KB |
| `noise_table15[1024]` u32 | sound.c | 4 KB |
| `obj_priority_list[5][160][128]` | video.cc | 100 KB |
| `obj_priority_count`, `obj_alpha_count`, misc | video.cc | ~1 KB |
| `reg[64]`, `spsr`, `reg_mode`, timers, DMA state | cpu.cc/main.c | ~1 KB |
| **Total core static** | | **~1.10 MB** |

Dynamic (shell-controlled on Playdate):

| Allocation | Size | Notes |
|---|---|---|
| ROM page cache | `ROM_BUFFER_SIZE` × 1MB | plan 8 MB |
| `gba_screen_pixels` | 75.5 KB | 240×161×u16 |
| Audio ring/staging for resampler | ~32 KB | 65536→44100 Hz conversion |
| Playdate render tables (Phase 2: lum LUT, dither buffers) | ~8 KB | |

Dynarec-only (Phase 4, for later): `rom_branch_hash` 256 KB,
ROM translation cache 2 MB + RAM translation cache 384 KB with
`SMALL_TRANSLATION_CACHE` (static buffers; no mmap on device, `memmap.c`
unused), plus I/D-cache maintenance after emit.

Excluded from Playdate build: `gba_cc_lut.c` (128 KB rodata),
libretro-common (except tiny VFS shim), `gba_processed_pixels`/
`gba_screen_pixels_prev` post-processing buffers (~151 KB saved vs libretro).

### 16 MB budget table (interpreter build)

Estimates marked (est.) until measured on device — measure in Phase 1.

| Item | Budget |
|---|---|
| pdex `.text` + `.rodata` (interpreter build) | ~1.5 MB (est.) |
| Core static `.data`/`.bss` (table above) | 1.1 MB |
| ROM page cache (`ROM_BUFFER_SIZE=8`) | 8.0 MB |
| Screen buffer + render tables | ~0.1 MB |
| Audio (ring + resample staging) | ~0.1 MB |
| ROM picker (pd-rom-picker) + file listing | ~0.1 MB |
| Playdate OS/SDK reserve + heap slack + stacks | remainder ≈ 5 MB |

If measurement shows more headroom, grow the ROM cache (12MB would cover
16MB ROMs half-resident); if less, shrink to 4MB before touching anything else.
Rev B external RAM is slower — page-cache misses hit harder there; do not
assume Rev B behaves like Rev A in paging benchmarks.

### Audio survey note

Core mixes at a fixed 65536 Hz stereo s16 (`GBA_SOUND_FREQUENCY`,
`sound_frequency` is const). Playdate output is 44100 Hz. Shell must
downsample ~1.486:1 (linear interp to start). Compile-time stub switch
(`-DAUDIO_DISABLED` style) planned so Phase 3 can isolate audio cost.

### Frame pacing (tentative decision, finalize in Phase 1)

GBA refresh 59.7275 Hz; Sharp LCD caps at 50 Hz. Plan: emulate at true
speed (audio stays correct), present at `setRefreshRate(50)` — effectively
drops ~10 emulated frames/s at the display, which combines naturally with
frameskip. Rejected alternative: running the emulated clock at 50/59.73×
(everything 16% slow, audio pitch drops or needs resample-tuning per game).

### L/R buttons (proposal, revisit in Phase 1)

- Primary: crank. Undocked crank, forward rotation past a threshold = R
  held, backward = L held; docked = neither. Cheap to read per frame.
- Fallback via System Menu option: "shoulder swap" mapping B→L or A→R for
  games that use one shoulder button heavily (e.g. Wario Land 4 dash = R).
- Deferred: chorded d-pad+B combos felt bad in previous ports; not planned.

### Phase 1 risk list

- `cpu.cc` / `video.cc` are C++ TUs upstream (built with `g++` in the
  libretro Makefile). Check whether they compile as C99/C11 with
  arm-none-eabi-gcc or need `-x c++ -fno-exceptions -fno-rtti` in the
  Playdate Makefile. No STL usage spotted in the survey.
- `serial.c`/`gbp.c` symbol pull-in from `rfu.c`/`serial_proto.c` — find the
  minimal compile set with serial disabled.
- Endianness/alignment assumptions are fine (ARM little-endian on both), but
  M7 unaligned access to strongly-ordered regions can fault — watch DMA/IO
  paths if anything is placed in unusual memory.
- `update_gba` assumes the shell calls `execute_arm(execute_cycles)` with the
  *global* `execute_cycles` (set by `init_main`/previous frame) — copy the
  libretro call shape exactly.

### Device results (preliminary — not yet in the ms format below)

- 2026-07-12, build bfa62fc, **Rev B**: FireRed boots and plays (menus
  tested). **10–11 FPS** via drawFPS — full render every frame, no
  frameskip, naive per-pixel blit, audio drained. That is ~90–100 ms per
  frame all-in; the emu/PPU/blit split is unknown until the Phase 3
  instrumentation lands. Stability beyond menus untested.

### Path to 30 FPS (assessment after first device run)

Rough math: 10.5 FPS ≈ 95 ms/frame. Even if render+blit were free,
the interpreter alone likely sits at ~15-18 FPS. So:

1. **Phase 2 renderer + frameskip** (fast LUT blit, skip PPU work on
   skipped frames — `skip_next_frame` already gates `update_scanline`):
   expected to land somewhere near 15-20 FPS rendered-at-25 with skip 1.
   This should clear the Phase 4 gate (≥ ~15 FPS with frameskip).
2. **Phase 3 instrumentation** decides where the remaining time goes
   (written to `perf.log` on the device Data partition, retrievable over
   the data disk — no serial console needed).
3. **Phase 4 Thumb-2 dynarec is the real path to 30 FPS.** gpSP's dynarec
   historically gives 2-4x over the interpreter on comparable cores; at
   2.5x the CPU side plus frameskip, 30 FPS for mode-0 games is plausible.
   Not a promise — it will be measured, and affine-heavy games stay out
   of scope.

### Scripted benchmarks (pd-playbench)

`make clean && make BENCH=1` builds with the pd-playbench submodule enabled
(same integration as nofrendo): input comes from a script, frame times are
collected, and a report (`pd-playbench-report.txt`, Data folder) is written
when the script ends. Script resolution order: `/Shared/Emulation/gba/
bench_script.txt` (swap over the data disk, no rebuild), else the bundled
`Source/bench_firered_intro.txt` (Start presses through the title, then
A-mash through the onboarding dialogs, ~2300 update frames). Script `UP`
bridges to GBA **Start** while a script runs (PDButtons has no Start —
nofrendo's UP→Start convention). Normal builds compile the same shell calls
against the header's no-op stubs; `make clean` when toggling BENCH.

### Benchmark format (Phase 3 fills this in)

```
## Bench YYYY-MM-DD <git-sha> <RevA|RevB>
Game            | emu ms | ppu ms | 1bit+blit ms | audio ms | total ms | skips
----------------|--------|--------|--------------|----------|----------|------
Wario Land 4    |        |        |              |          |          |
```

## Bench 2026-07-14 104b2df RevB (perf pass: ADDW cycles, O3 translator)
avg_frame_ms=29.42 (was 29.86) | est fps=33.99 | worst 117 -> steady-state 40

- perf.log diagnostics: **ramflush=0** (no SMC flush storms - that theory is
  closed); spikes are cold translation and plateau once romtx stops growing
  (709KB of the 2MB ROM cache after the intro; max drops 111 -> 40ms).
- Budget now: emu 25-28ms, aud 1.9ms, blit 3ms. All remaining headroom is
  in the emulation core itself. Next levers, in order of expected value:
  1. TCM relocation of the hot stub memory handlers (every guest load/store;
     emitted code thrashes the 16KB I-cache - vecx-class +10-15% expected).
  2. Inline EWRAM/IWRAM fastpaths in emitted code (bigger win, bigger
     surgery on the emitter).
  3. Frame-pacing catch-up only helps where headroom exists (verified:
     intro shows no change; menus/dialogue reach true 59.73).

## Bench 2026-07-13 1de1190 RevB (DYNAREC + AUDIO, FireRed, bundled script)
avg_frame_ms=29.90 best=15 worst=108 | est fps=33.45 | skipped 1642/2300

- Audio costs ~2.2ms/frame (29.90 vs 27.71 without). Confirmed audible on
  device: music plays, stutters/gaps while under speed (by design - pitch
  stays correct), some distortion to investigate later.

## Bench 2026-07-13 7bff380 RevB (DYNAREC, FireRed, bundled script)
avg_frame_ms=27.71 best=12 worst=112 | est fps=36.09 | skipped 1465/2300

- **2.48x over the interpreter baseline** (68.68ms -> 27.71ms), full script,
  no crashes. Same-script head-to-head, same game, same device.
- Crash on the first attempt: frontend writes 8-byte block headers with
  STRD into the translation stream; T32's 16-bit forms can leave the
  cursor 2-aligned -> UsageFault (UNALIGNED). Fixed: align_translation_ptr()
  at block boundaries (backend-provided macro, no-op for other backends).
- worst=112ms spikes = translation bursts (cold cache) - expected.
- Next levers: idle-loop elimination (dynarec-only, big for Pokemon),
  -O3/ITCM experiments, then audio.

## Bench 2026-07-13 f564655 RevB (DYNAREC first run, Tetris homebrew, bundled script)
avg_frame_ms=17.37 best=4 worst=37 | est fps=57.58 | skipped 1142/2300

- FIRST EXECUTION of the Thumb-2 dynarec: full 2300-frame scripted run,
  zero crashes (boot, BIOS SWIs, IRQs, memory handlers, gameplay all
  exercised). Not comparable to the FireRed interpreter baseline (different
  game) - FireRed dynarec run is next.

## Bench 2026-07-12 15d5c3b RevB (pd-playbench, bundled FireRed intro script)
avg_frame_ms=68.68 best=16 worst=263 | est fps=14.56 | skipped 1694/2300 (74%)

- First scripted run — THE baseline all future builds compare against.
- Includes the PSG-mixing removal + -O3 interpreter: only a few ms better
  than the c914e1f manual run below (and a different workload) → the
  interpreter core is the bottleneck, confirmed.
- ≈ the Phase-4 gate (~15fps w/ skip). Phase 4 (Thumb-2 dynarec) is a go.

## Bench 2026-07-12 c914e1f RevB (perf.log, fs=auto, FireRed intro+onboarding)
Game            | emu ms (CPU+PPU) | 1bit+blit ms | upd/s | skips
----------------|------------------|--------------|-------|------
Pokemon FireRed | 60-80 (max 242)  | 3.5-3.65     | 13-16 | 448-450/600

- "emu" is execute_arm inclusive: CPU interpreter + PPU on the 1-of-4
  rendered frames + PSG mixing. Blit is solved (3.6ms, was the naive path's
  cost). Game speed ≈ 22-27% real time — frameskip cannot fix game speed,
  only render rate; the CPU side must get ~3x faster for 30 FPS.
- Conclusion: at/near the Phase-4 gate (13-16 fps w/ skip). Next: strip the
  discarded PSG mixing out of the frame (pure waste until audio output
  exists), try -O3 on cpu.o, re-measure; then start the Thumb-2 dynarec.

Numbers in ms per frame, measured on device with `pd->system->getElapsedTime`
deltas averaged over ≥10s of gameplay (not menus). Never fps, never simulator.

### Known limitations (running list)

- Affine-heavy games (Mario Kart Super Circuit) out of scope.
- Multiplayer (serial/RFU/GBP rumble) not planned.
- 50 Hz display cap means ~10 dropped frames/s even at full speed.
