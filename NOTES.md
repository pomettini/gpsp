# gpSP → Playdate port notes

Working notes for the Playdate port. Updated every session.
Conventions follow my other ports (vecx, pokemini, nofrendo, red-viper):
C only, Playdate glue in `playdate_main.c` at repo root, diff against
upstream kept minimal, libretro build must keep working.

## Status

- **Phase 0 (survey): done** — this document.
- Phase 1 (interpreter port): not started.
- Phase 2 (renderer): not started.
- Phase 3 (profiling): not started.
- Phase 4 (Thumb-2 dynarec): gated on Phase 3 results.

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

### Benchmark format (Phase 3 fills this in)

```
## Bench YYYY-MM-DD <git-sha> <RevA|RevB>
Game            | emu ms | ppu ms | 1bit+blit ms | audio ms | total ms | skips
----------------|--------|--------|--------------|----------|----------|------
Wario Land 4    |        |        |              |          |          |
```

Numbers in ms per frame, measured on device with `pd->system->getElapsedTime`
deltas averaged over ≥10s of gameplay (not menus). Never fps, never simulator.

### Known limitations (running list)

- Affine-heavy games (Mario Kart Super Circuit) out of scope.
- Multiplayer (serial/RFU/GBP rumble) not planned.
- 50 Hz display cap means ~10 dropped frames/s even at full speed.
