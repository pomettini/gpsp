/* Playdate shell for gpSP. Replaces the libretro frontend: owns init, ROM
 * selection (pd-rom-picker), the input callback, the frame loop, save-RAM
 * persistence and the hand-off to render.c. Interpreter-only (Phase 1).
 * Plan/decisions in NOTES.md. */

#include <stdint.h>
#include <string.h>

#include "pd_api.h"

#include "common.h"
#include "render.h"
#include "rom_picker.h"
#include "pd_playbench.h"
#ifdef PD_MEM_PROFILE
#include "pd_memprof.h"
#endif

void pd_filestream_init(PlaydateAPI *pd);
#ifdef TARGET_PLAYDATE
extern PlaydateAPI *pd_syscalls_pd;
#endif
#ifdef HAVE_DYNAREC
/* cpu_threaded.c calls this after each emit (whole-cache clearICache). */
extern void (*thumb2_cache_sync_cb)(void);
#endif

/* Globals the core expects the frontend to define (libretro.c upstream). */
int dynarec_enable = 0;
u32 skip_next_frame = 0;
boot_mode selected_boot_mode = boot_game;
int sprite_limit = 1;
u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets = 0;
/* Netplay is a libretro-frontend feature; serial code checks these and
 * stays inert with 0 clients / no packet callbacks. */
u32 netplay_num_clients = 0, netplay_client_id = 0;
void netpacket_poll_receive(void) {}
void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
  (void)client_id; (void)buf; (void)len;
}

void set_fastforward_override(bool fastforward) { (void)fastforward; }

static PlaydateAPI *pd;

/* Core render target: 240x161 RGB565 (one slack row for winobj, see
 * GBA_SCREEN_BUFFER_SIZE). Static: it is a fixed cost, keep it out of the
 * heap so the ROM cache gets an honest malloc budget. */
static u16 screen_pixels[GBA_SCREEN_BUFFER_SIZE / sizeof(u16)];

/* Audio output lives in pd_audio.c: main thread mixes+resamples into a
 * ring, the Playdate audio thread drains it. */
void pd_audio_init(PlaydateAPI *pd);
void pd_audio_frame(void);

/* --- Input ----------------------------------------------------------------
 * input.c polls through a libretro-style callback; we provide one that maps
 * Playdate buttons to a retropad bitmask, so input.c stays untouched.
 * GBA Start/Select have no physical buttons: crank flicks (forward = Start,
 * backward = Select) and System Menu items inject timed presses.
 * L/R are deferred (proposal in NOTES.md). */

#define INJECT_FRAMES 8
static int start_frames;
static int select_frames;
static float crank_accum;

static int16_t pd_input_state(unsigned port, unsigned device,
                              unsigned index, unsigned id)
{
  PDButtons cur;
  int16_t mask = 0;

  if (port != 0 || device != RETRO_DEVICE_JOYPAD)
    return 0;
  (void)index;

  pd->system->getButtonState(&cur, NULL, NULL);

  /* Bench builds: scripted input replaces the real buttons while a script
   * runs. Script UP bridges to GBA Start (nofrendo's UP->Start convention;
   * PDButtons has no Start and playbench's MENU token never reaches
   * PDButtons), so benchmark scripts use UP wherever Start is meant. */
  cur = pd_playbench_get_buttons(cur);
  if (pd_playbench_is_running() && (cur & kButtonUp))
  {
    cur &= ~kButtonUp;
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_START;
  }

  if (cur & kButtonUp)    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
  if (cur & kButtonDown)  mask |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
  if (cur & kButtonLeft)  mask |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
  if (cur & kButtonRight) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
  if (cur & kButtonA)     mask |= 1 << RETRO_DEVICE_ID_JOYPAD_A;
  if (cur & kButtonB)     mask |= 1 << RETRO_DEVICE_ID_JOYPAD_B;

  if (start_frames > 0)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_START;
  if (select_frames > 0)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_SELECT;

  if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
    return mask;
  return (mask >> id) & 1;
}

static void poll_crank_and_injections(void)
{
  if (!pd->system->isCrankDocked())
  {
    crank_accum += pd->system->getCrankChange();
    if (crank_accum > 45.0f)
    {
      start_frames = INJECT_FRAMES;
      crank_accum = 0.0f;
    }
    else if (crank_accum < -45.0f)
    {
      select_frames = INJECT_FRAMES;
      crank_accum = 0.0f;
    }
  }

  if (start_frames > 0)
    start_frames--;
  if (select_frames > 0)
    select_frames--;
}

/* Perf accumulation, dumped to perf.log on the Data partition every
 * PERF_WINDOW updates (retrieved over the data disk; no serial needed).
 * Times in ms via getCurrentTimeMilliseconds (1ms granularity; the window
 * totals are what matters). */
#define PERF_WINDOW 600
static u32 perf_updates, perf_rendered, perf_skipped;
static u32 perf_emu_r_ms, perf_emu_s_ms;
static u32 perf_guest_frames, perf_last_frame_counter;
static u32 perf_emu_ms, perf_blit_ms, perf_aud_ms, perf_window_start_ms;
static u32 perf_emu_max_ms;

#ifdef PD_M4A_HLE
extern u32 pd_m4a_hle_matched;
static int pd_m4a_hle_logged;
#endif
#ifdef PD_FIRERED_SPRITE_HLE
extern u32 pd_firered_hle_matched;
static int pd_firered_hle_logged;
#endif
#ifdef PD_FIRERED_IRQ_HLE
extern u32 pd_firered_irq_matched;
static int pd_firered_irq_logged;
#endif
#ifdef PD_IWRAM_STACK_FAST
extern u32 pd_iwram_stack_fast_active;
static int pd_iwram_stack_fast_logged;
#endif

#if defined(PD_M4A_DUMP) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
static u32 pd_m4a_dump_frames;
static int pd_m4a_dumped;
#endif

#if defined(PD_MEM_PROFILE) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
typedef struct
{
  char magic[8];
  uint32_t version;
  uint32_t period;
  uint32_t count;
  uint32_t dropped;
  uint32_t capacity;
} PDMemProfileHeader;

static int pd_memprof_dumped;

static void pd_memprof_reset(void)
{
  pd_memprof_count = 0;
  pd_memprof_dropped = 0;
  reg[PD_MEMPROF_COUNT_REG] = PD_MEMPROF_INITIAL;
  pd_memprof_dumped = 0;
  pd->system->logToConsole("gpsp memprof: sampling 1/%u, capacity %u",
                           (unsigned)PD_MEMPROF_PERIOD,
                           (unsigned)PD_MEMPROF_CAPACITY);
}

static void pd_memprof_dump(void)
{
  PDMemProfileHeader header = {
    {'G', 'P', 'S', 'P', 'M', 'E', 'M', '1'},
    1,
    PD_MEMPROF_PERIOD,
    pd_memprof_count,
    pd_memprof_dropped,
    PD_MEMPROF_CAPACITY
  };
  SDFile *f = pd->file->open("memprof.bin", kFileWrite);

  pd_memprof_dumped = 1;
  if (!f)
  {
    pd->system->logToConsole("gpsp memprof: cannot write memprof.bin");
    return;
  }

  pd->file->write(f, &header, sizeof(header));
  pd->file->write(f, pd_memprof_records,
                  (unsigned int)(header.count * sizeof(pd_memprof_records[0])));
  pd->file->close(f);
  pd->system->logToConsole("gpsp memprof: wrote %u samples, dropped %u",
                           (unsigned)header.count, (unsigned)header.dropped);
}
#endif

/* --- Save RAM -------------------------------------------------------------
 * gamepak_backup (SRAM/Flash/EEPROM, 128KB) persists as <rom>.sav in the
 * Data folder. Written on terminate/lock/back-to-picker; the actual backup
 * type only uses a prefix of the buffer but saving it whole is simpler and
 * compatible. */

static char save_path[ROM_PICKER_MAX_PATH + 4];
static int rom_loaded;

static void make_save_path(const char *rom_path)
{
  const char *base = strrchr(rom_path, '/');
  size_t len;

  base = base ? base + 1 : rom_path;
  strncpy(save_path, base, sizeof(save_path) - 5);
  save_path[sizeof(save_path) - 5] = '\0';

  len = strlen(save_path);
  if (len > 4 && save_path[len - 4] == '.')
    save_path[len - 4] = '\0';
  strcat(save_path, ".sav");
}

static void load_save_ram(void)
{
  SDFile *f = pd->file->open(save_path, kFileReadData);
  int total = 0;

  if (!f)
    return;

  while (total < (int)sizeof(gamepak_backup))
  {
    int got = pd->file->read(f, gamepak_backup + total,
                             sizeof(gamepak_backup) - total);
    if (got <= 0)
      break;
    total += got;
  }
  pd->file->close(f);
  pd->system->logToConsole("gpsp: loaded %s (%d bytes)", save_path, total);
}

static void write_save_ram(void)
{
  SDFile *f;

  if (!rom_loaded)
    return;

  /* Never clobber a real save file with erased flash: a game-issued
   * chip erase (or a save-detect misfire) leaves the buffer all-0xFF,
   * and persisting that once destroyed a real save (2026-07-20). */
  {
    u32 i;
    for (i = 0; i < sizeof(gamepak_backup); i++)
      if (gamepak_backup[i] != 0xFF)
        break;
    if (i == sizeof(gamepak_backup))
    {
      SDFile *old_f = pd->file->open(save_path, kFileReadData);
      if (old_f)
      {
        pd->file->close(old_f);
        pd->system->logToConsole("gpsp: NOT writing %s (backup is erased)",
                                 save_path);
        return;
      }
    }
  }

  f = pd->file->open(save_path, kFileWrite);
  if (!f)
  {
    pd->system->logToConsole("gpsp: cannot write %s", save_path);
    return;
  }
  pd->file->write(f, gamepak_backup, sizeof(gamepak_backup));
  pd->file->close(f);
  pd->system->logToConsole("gpsp: wrote %s", save_path);
}

/* --- ROM picker / lifecycle ---------------------------------------------- */

static const char *rom_extensions[] = { "gba", "agb", NULL };
static int picker_active = 1;
static int want_picker = 0;
static char selected_rom[ROM_PICKER_MAX_PATH];

static void on_rom_picked(const char *path, void *userdata)
{
  (void)userdata;
  strncpy(selected_rom, path, sizeof(selected_rom) - 1);
  selected_rom[sizeof(selected_rom) - 1] = '\0';
}

static void init_rom_picker(void)
{
  RomPickerConfig cfg;

  cfg.folder = "/Shared/Emulation/gba/games/";
  cfg.extensions = rom_extensions;
  cfg.on_select = on_rom_picked;
  cfg.userdata = NULL;
  cfg.auto_load_single = 1;
  rom_picker_init(pd, &cfg);
}

static void rompicker_menu_cb(void *userdata)
{
  (void)userdata;
  want_picker = 1;
}

static void start_menu_cb(void *userdata)
{
  (void)userdata;
  start_frames = INJECT_FRAMES;
}

/* Frameskip: 0=auto (skip while running behind), 1=off, 2..4=fixed 1..3
 * skipped frames per rendered one. Skipped frames bypass the PPU entirely
 * (update_scanline returns on skip_next_frame) and the blit. */
static int frameskip_mode = 0;
static PDMenuItem *fs_item;
static const char *fs_options[] = { "auto", "off", "1", "2", "3" };

static void frameskip_menu_cb(void *userdata)
{
  (void)userdata;
  frameskip_mode = pd->system->getMenuItemValue(fs_item);
}

static void rebuild_menu(int in_picker)
{
  pd->system->removeAllMenuItems();
  fs_item = NULL;
  if (!in_picker)
  {
    pd->system->addMenuItem("ROM Picker", rompicker_menu_cb, NULL);
    pd->system->addMenuItem("Press Start", start_menu_cb, NULL);
    fs_item = pd->system->addOptionsMenuItem("Frameskip", fs_options, 5,
                                             frameskip_menu_cb, NULL);
    pd->system->setMenuItemValue(fs_item, frameskip_mode);
  }
}

static void start_emulation(void)
{
  /* Always the embedded open-source BIOS (bios_data.S); no BIOS file. */
  memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));
  memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));

  if (load_gamepak(NULL, selected_rom, FEAT_AUTODETECT, FEAT_DISABLE,
                   SERIAL_MODE_DISABLED) != 0)
  {
    pd->system->logToConsole("gpsp: FAILED to load %s", selected_rom);
    selected_rom[0] = '\0';
    return;
  }

  make_save_path(selected_rom);
  load_save_ram();

  reset_gba();
  rom_loaded = 1;

#if defined(PD_TCM_POOL) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
  /* After init_emitter refilled the dispatch tables: relocate the handler
   * pool to DTCM and repoint the tables (see pd_tcm_pool.c). */
  {
    extern void pd_tcm_pool_install(PlaydateAPI *pd);
    pd_tcm_pool_install(pd);
  }
#endif

  rom_picker_free();
  pd->graphics->clear(kColorBlack);
  want_picker = 0;
  picker_active = 0;
  rebuild_menu(0);
  perf_updates = perf_rendered = perf_skipped = 0;
  perf_emu_ms = perf_blit_ms = perf_emu_max_ms = 0;
  perf_emu_r_ms = perf_emu_s_ms = 0;
  perf_last_frame_counter = frame_counter;
  perf_window_start_ms = pd->system->getCurrentTimeMilliseconds();
  pd->system->logToConsole("gpsp: running %s", selected_rom);

#ifdef PD_M4A_HLE
  pd_m4a_hle_matched = 0;
  pd_m4a_hle_logged = 0;
#endif
#ifdef PD_FIRERED_SPRITE_HLE
  pd_firered_hle_matched = 0;
  pd_firered_hle_logged = 0;
#endif
#ifdef PD_FIRERED_IRQ_HLE
  pd_firered_irq_matched = 0;
  pd_firered_irq_logged = 0;
#endif
#ifdef PD_IWRAM_STACK_FAST
  pd_iwram_stack_fast_active = 0;
  pd_iwram_stack_fast_logged = 0;
#endif

#if defined(PD_M4A_DUMP) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
  pd_m4a_dump_frames = 0;
  pd_m4a_dumped = 0;
#endif

#if defined(PD_MEM_PROFILE) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
  pd_memprof_reset();
#endif

#if defined(PD_SCHED_STATS) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
  /* Warm-lookup microbenchmark: how much does one runtime block lookup
   * cost? (The BIOS SWI block at 0x8 is always translated by now.) */
  {
    extern u8 function_cc *block_lookup_address_arm(u32 pc);
    volatile u8 *pd_bp;
    int pd_i;
    pd->system->resetElapsedTime();
    float pd_t0 = pd->system->getElapsedTime();
    for (pd_i = 0; pd_i < 10000; pd_i++)
      pd_bp = block_lookup_address_arm(0x8);
    float pd_t1 = pd->system->getElapsedTime();
    pd->system->logToConsole("gpsp: lookup ubench %.2fus/call",
                             (double)((pd_t1 - pd_t0) * 100.0f));
    (void)pd_bp;
  }
#endif

  /* Bench builds (make BENCH=1): scripted input + frame-time report,
   * written to pd-playbench-report.txt in the Data folder when the script
   * finishes. A /Shared script overrides the bundled one so benchmarks can
   * be swapped over the data disk without rebuilding. */
  {
    PDBenchConfig cfg = {0};
    cfg.test_name = "scripted-run";
    cfg.emulator_name = "gpsp";
    cfg.rom_name = selected_rom;
    cfg.build_label = __DATE__ " " __TIME__;
    cfg.device_label = "RevB";
    cfg.target_fps = 50;
    cfg.log_to_console = 1;
    cfg.write_report_file = 1;
    cfg.input_mode = PD_PLAYBENCH_INPUT_OVERRIDE;
    pd_playbench_init(pd, &cfg);
    if (pd_playbench_load_script_from_file("/Shared/Emulation/gba/bench_script.txt"))
      pd->system->logToConsole("gpsp bench: loaded /Shared script");
    else if (pd_playbench_load_script_from_file("bench_firered_intro.txt"))
      pd->system->logToConsole("gpsp bench: loaded bundled script");
    else
      pd->system->logToConsole("gpsp bench: no script (%s)",
                               pd_playbench_get_last_error());
    pd_playbench_start();
  }
}

static void return_to_picker(void)
{
  write_save_ram();
  rom_loaded = 0;
  selected_rom[0] = '\0';
  want_picker = 0;
  pd->graphics->clear(kColorWhite);
  init_rom_picker();
  picker_active = 1;
  rebuild_menu(1);
}

/* --- Frame loop ----------------------------------------------------------- */

static void perf_flush(u32 now)
{
  perf_guest_frames = frame_counter - perf_last_frame_counter;
  SDFile *f = pd->file->open("perf.log", kFileAppend);
  if (f)
  {
    char line[260];
    u32 wall = now - perf_window_start_ms;
    int len = snprintf(line, sizeof(line),
        "build %s %s | fs=%s | upd=%u rend=%u skip=%u | wall=%ums "
        "| emu avg=%u.%02ums max=%ums | aud avg=%u.%02ums | blit avg=%u.%02ums\n",
        __DATE__, __TIME__, fs_options[frameskip_mode],
        (unsigned)perf_updates, (unsigned)perf_rendered,
        (unsigned)perf_skipped, (unsigned)wall,
        (unsigned)(perf_emu_ms / perf_updates),
        (unsigned)((perf_emu_ms * 100 / perf_updates) % 100),
        (unsigned)perf_emu_max_ms,
        (unsigned)(perf_aud_ms / perf_updates),
        (unsigned)((perf_aud_ms * 100 / perf_updates) % 100),
        (unsigned)(perf_rendered ? perf_blit_ms / perf_rendered : 0),
        (unsigned)(perf_rendered ? (perf_blit_ms * 100 / perf_rendered) % 100 : 0));
  if (len > 0 && line[len - 1] == '\n')
    len--;
  len += snprintf(line + len, sizeof(line) - len,
                  " | emuR=%u.%01ums emuS=%u.%01ums gfps=%u.%01u\n",
                  (unsigned)(perf_rendered ? perf_emu_r_ms / perf_rendered : 0),
                  (unsigned)(perf_rendered ?
                             (perf_emu_r_ms * 10 / perf_rendered) % 10 : 0),
                  (unsigned)(perf_skipped ? perf_emu_s_ms / perf_skipped : 0),
                  (unsigned)(perf_skipped ?
                             (perf_emu_s_ms * 10 / perf_skipped) % 10 : 0),
                  (unsigned)(wall ? (perf_guest_frames * 1000) / wall : 0),
                  (unsigned)(wall ?
                             ((perf_guest_frames * 10000) / wall) % 10 : 0));
#ifdef HAVE_DYNAREC
    /* Spike diagnostics: SMC-triggered RAM-cache flushes this window and
     * ROM translation-cache fill level. */
    {
      extern u32 flush_ram_count;
      extern u8 *rom_translation_ptr;
      extern u8 rom_translation_cache[];
      static u32 last_flush_count;
      if (len > 0 && line[len - 1] == '\n')
        len--;                       /* keep everything on one line */
      len += snprintf(line + len, sizeof(line) - len,
                      " | ramflush=%u romtx=%uKB\n",
                      (unsigned)(flush_ram_count - last_flush_count),
                      (unsigned)((rom_translation_ptr -
                                  rom_translation_cache) >> 10));
      last_flush_count = flush_ram_count;
    }
#endif
#ifdef PD_SCHED_STATS
    {
      /* Scheduler round-trips: calls per guest frame and the sampled
       * per-call cost, extrapolated to ms per frame. */
      extern u32 pd_updgba_calls, pd_updgba_sampled_us, pd_updgba_samples;
      extern u32 pd_lookup_calls;
      extern u32 pd_ff_lines;
      static u32 last_ff;
      static u32 last_lookups;
      static u32 last_calls, last_us, last_samples;
      u32 dcalls = pd_updgba_calls - last_calls;
      u32 dus = pd_updgba_sampled_us - last_us;
      u32 dsamples = pd_updgba_samples - last_samples;
      u32 uspercall = dsamples ? dus / dsamples : 0;
      u32 est_us_frame = (dcalls / (perf_updates ? perf_updates : 1)) * uspercall;
      last_calls = pd_updgba_calls;
      last_us = pd_updgba_sampled_us;
      last_samples = pd_updgba_samples;
      if (len > 0 && line[len - 1] == '\n')
        len--;
      len += snprintf(line + len, sizeof(line) - len,
                      " | gba=%u/upd %uus est=%u.%01ums lookup=%u/upd\n",
                      (unsigned)(dcalls / (perf_updates ? perf_updates : 1)),
                      (unsigned)uspercall,
                      (unsigned)(est_us_frame / 1000),
                      (unsigned)((est_us_frame % 1000) / 100),
                      (unsigned)((pd_lookup_calls - last_lookups) /
                                 (perf_updates ? perf_updates : 1)));
      last_lookups = pd_lookup_calls;
      if (len > 0 && line[len - 1] == '\n')
        len--;
      len += snprintf(line + len, sizeof(line) - len,
                      " ff=%u/upd\n",
                      (unsigned)((pd_ff_lines - last_ff) /
                                 (perf_updates ? perf_updates : 1)));
      last_ff = pd_ff_lines;
    }
#endif
#if defined(PD_TCM_POOL) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
    {
      extern int pd_tcm_pool_ok(void);
      if (!pd_tcm_pool_ok())
      {
        if (len > 0 && line[len - 1] == '\n')
          len--;
        len += snprintf(line + len, sizeof(line) - len, " | POOL CANARY DEAD\n");
      }
    }
#endif
    pd->file->write(f, line, len);
    pd->file->close(f);
  }
  perf_updates = perf_rendered = perf_skipped = 0;
  perf_emu_ms = perf_blit_ms = perf_aud_ms = perf_emu_max_ms = 0;
  perf_emu_r_ms = perf_emu_s_ms = 0;
  perf_last_frame_counter = frame_counter;
  perf_window_start_ms = now;
}

/* Frameskip decision for this update. */
static int decide_skip(u32 last_update_ms)
{
  static int skip_run;

  if (frameskip_mode == 1) /* off */
  {
    skip_run = 0;
    return 0;
  }

  if (frameskip_mode >= 2) /* fixed 1..3 */
  {
    int n = frameskip_mode - 1;
    if (skip_run < n)
    {
      skip_run++;
      return 1;
    }
    skip_run = 0;
    return 0;
  }

  /* auto: skip while the previous update ran over the 20ms budget,
   * capped at 3 consecutive skips so the screen never freezes. */
  if (last_update_ms > 20 && skip_run < 3)
  {
    skip_run++;
    return 1;
  }
  skip_run = 0;
  return 0;
}

static int update(void *userdata)
{
  static u32 last_update_ms;
  u32 t0, t1, t_aud, t2;
  int skip;

  (void)userdata;

#ifdef PD_M4A_HLE
  if (pd_m4a_hle_matched && !pd_m4a_hle_logged)
  {
    pd_m4a_hle_logged = 1;
    pd->system->logToConsole("gpsp: FireRed m4a fast path active");
  }
#endif
#ifdef PD_FIRERED_SPRITE_HLE
  if (pd_firered_hle_matched && !pd_firered_hle_logged)
  {
    pd_firered_hle_logged = 1;
    pd->system->logToConsole("gpsp: FireRed sprite fast path active");
  }
#endif
#ifdef PD_FIRERED_IRQ_HLE
  if (pd_firered_irq_matched && !pd_firered_irq_logged)
  {
    pd_firered_irq_logged = 1;
    pd->system->logToConsole("gpsp: FireRed HBlank fast path active");
  }
#endif
#ifdef PD_IWRAM_STACK_FAST
  if (pd_iwram_stack_fast_active && !pd_iwram_stack_fast_logged)
  {
    pd_iwram_stack_fast_logged = 1;
    pd->system->logToConsole("gpsp: IWRAM stack fast path active");
  }
#endif

  if (picker_active)
  {
    if (selected_rom[0] == '\0')
      rom_picker_update();
    if (selected_rom[0] != '\0')
      start_emulation();
    return 1;
  }

  if (want_picker)
  {
    return_to_picker();
    return 1;
  }

  poll_crank_and_injections();
  update_input();

  skip = decide_skip(last_update_ms);
#ifdef PD_PPU_OFF
  /* PPU-share measurement build: never render, so perf.log's emu avg is
   * pure CPU+scheduler cost. The screen stays frozen by design. */
  skip = 1;
#endif

  /* Time-based catch-up: the GBA runs at 59.73 fps but updates arrive at
   * <=50Hz, so one guest frame per update caps the game at 84% speed. Track
   * frames owed by wall time (16.16-ish fixed point: 597 owed-units per ms,
   * 10000 per frame) and run a second guest frame when a full one is owed
   * and the first was cheap. The extra frame is never rendered. */
  t0 = pd->system->getCurrentTimeMilliseconds();
  {
    static u32 pace_last_ms;
    static u32 pace_owed;
    u32 pace_dt = t0 - pace_last_ms;
    int extra;
    pace_last_ms = t0;
    if (pace_dt > 100)
      pace_dt = 100;                /* menu pauses etc: don't accrue debt */
    pace_owed += pace_dt * 597;
    if (pace_owed > 25000)
      pace_owed = 25000;            /* cap the debt at 2.5 frames */

    /* Decide up front (from the previous update's cost) whether two guest
     * frames fit this window; the first of a pair is never rendered. Heavy
     * scenes always run and render exactly one frame. */
    {
      int run_two = (pace_owed >= 20000 && last_update_ms < 14);
      int nframes = run_two ? 2 : 1;

      for (extra = 0; extra < nframes; extra++)
      {
        /* Interpreter path needs the sticky bits cleared (page-eviction
         * protection) exactly like the libretro loop. */
        skip_next_frame = (run_two && extra == 0) ? 1 : skip;
#ifdef HAVE_DYNAREC
        if (dynarec_enable)
        {
          execute_arm_translate(execute_cycles);
        }
        else
#endif
        {
          clear_gamepak_stickybits();
          execute_arm(execute_cycles);
        }
        if (pace_owed >= 10000)
          pace_owed -= 10000;
        else
          pace_owed = 0;
      }
    }
  }
  t1 = pd->system->getCurrentTimeMilliseconds();

  pd_audio_frame();
  t_aud = pd->system->getCurrentTimeMilliseconds();

  if (!skip)
    pd_render_frame(screen_pixels);
  t2 = pd->system->getCurrentTimeMilliseconds();

  pd->system->drawFPS(0, 0);

  last_update_ms = t2 - t0;
  pd_playbench_update();
  pd_playbench_report_frame((float)last_update_ms, skip ? 1 : 0);

#if defined(PD_MEM_PROFILE) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
  if (!pd_memprof_dumped && pd_playbench_is_finished())
    pd_memprof_dump();
#endif

#if defined(PD_M4A_DUMP) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
  if (!pd_m4a_dumped && ++pd_m4a_dump_frames == 300)
  {
    extern u8 iwram[];
    SDFile *fiw = pd->file->open("iwram.bin", kFileWrite);
    pd_m4a_dumped = 1;
    if (fiw)
    {
      /* The first 32KB is the dynarec's SMC shadow; guest IWRAM follows. */
      pd->file->write(fiw, iwram + 0x8000, 0x8000);
      pd->file->close(fiw);
      pd->system->logToConsole("gpsp: dumped iwram.bin");
    }
    else
    {
      pd->system->logToConsole("gpsp: cannot write iwram.bin");
    }
  }
#endif

#ifdef PD_FRAME_DUMP
  /* Diagnostic: dump the raw RGB565 guest frame every ~10s so visual
   * bugs can be inspected off-device (Data/frame.bin, 240x160x2). */
  {
    static u32 pd_fd_count;
    if (++pd_fd_count >= 300)
    {
      SDFile *fdf = pd->file->open("frame.bin", kFileWrite);
      pd_fd_count = 0;
      if (fdf)
      {
        pd->file->write(fdf, gba_screen_pixels, 240 * 160 * 2);
        pd->file->close(fdf);
        pd->system->logToConsole("gpsp: dumped frame.bin");
      }
    }
  }
#endif

  perf_updates++;
  perf_emu_ms += t1 - t0;
  perf_aud_ms += t_aud - t1;
  if (t1 - t0 > perf_emu_max_ms)
    perf_emu_max_ms = t1 - t0;
  if (skip)
  {
    perf_skipped++;
    perf_emu_s_ms += t1 - t0;
  }
  else
  {
    perf_rendered++;
    perf_emu_r_ms += t1 - t0;
    perf_blit_ms += t2 - t_aud;
  }
  if (perf_updates >= PERF_WINDOW)
    perf_flush(t2);

#if defined(TARGET_SIMULATOR) && defined(HAVE_DYNAREC)
  /* Translation-dump harness: the translator is pure C, so the host build
   * can translate real guest blocks; the emitted Thumb-2 is dumped (never
   * executed) and disassembled offline. See NOTES.md Phase 4. */
  {
    extern u8 rom_translation_cache[];
    extern u8 *rom_translation_ptr;
    u8 *block_lookup_address_arm(u32 gpc);
    u8 *block_lookup_address_thumb(u32 gpc);
    static int jit_dumped;
    static int jit_frames;

#ifndef PD_JITDUMP_FRAME
#define PD_JITDUMP_FRAME 300
#endif
    if (!jit_dumped && ++jit_frames == PD_JITDUMP_FRAME)
    {
      u8 *entry = block_lookup_address_arm(0x08000000);
      u8 *cur;
      char msg[120];
      SDFile *f;

      if (reg[REG_CPSR] & 0x20)
        cur = block_lookup_address_thumb(reg[REG_PC]);
      else
        cur = block_lookup_address_arm(reg[REG_PC]);

      f = pd->file->open("jitdump.bin", kFileWrite);
      if (f)
      {
        pd->file->write(f, rom_translation_cache,
                        (unsigned int)(rom_translation_ptr -
                                       rom_translation_cache));
        pd->file->close(f);
      }
      f = pd->file->open("jitdump.txt", kFileWrite);
      if (f)
      {
        int n = snprintf(msg, sizeof(msg),
                         "entry(0x08000000)@+0x%x cur(pc=0x%x thumb=%d)@+0x%x len=0x%x\n",
                         (unsigned)(entry - rom_translation_cache),
                         (unsigned)reg[REG_PC],
                         (int)((reg[REG_CPSR] >> 5) & 1),
                         (unsigned)(cur - rom_translation_cache),
                         (unsigned)(rom_translation_ptr - rom_translation_cache));
        pd->file->write(f, msg, n);
        pd->file->close(f);
      }
      pd->system->logToConsole("gpsp: jit dump written");
      jit_dumped = 1;
    }
  }
#endif

#if defined(PD_JIT_DUMP_DEVICE) && defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
  /* Emitted-code corpus dump: near the end of a bench run, write the ROM
   * translation cache, its branch hash table (block enumeration) and the
   * RAM cache for offline statistical analysis of emitter output. */
  {
    extern u8 rom_translation_cache[];
    extern u8 *rom_translation_ptr;
    extern u8 ram_translation_cache[];
    extern u8 *ram_translation_ptr;
    extern u32 rom_branch_hash[];
    static int jd_done;
    static u32 jd_frames;
    if (!jd_done && ++jd_frames == 4400)
    {
      SDFile *f;
      f = pd->file->open("jitrom.bin", kFileWrite);
      if (f)
      {
        pd->file->write(f, rom_translation_cache,
                        (unsigned int)(rom_translation_ptr -
                                       rom_translation_cache));
        pd->file->close(f);
      }
      f = pd->file->open("jithash.bin", kFileWrite);
      if (f)
      {
        pd->file->write(f, rom_branch_hash, (1 << 16) * 4);
        pd->file->close(f);
      }
      f = pd->file->open("jitram.bin", kFileWrite);
      if (f)
      {
        pd->file->write(f, ram_translation_cache,
                        (unsigned int)(ram_translation_ptr -
                                       ram_translation_cache));
        pd->file->close(f);
      }
      pd->system->logToConsole("gpsp: jit corpus dumped");
      jd_done = 1;
    }
  }
#endif

#ifdef TARGET_SIMULATOR
  /* Headless-verification hook: dump the raw RGB565 frame a few times so
   * the boot can be checked offline (see NOTES.md Phase 1). */
  {
    static int dump_frame_no;
    dump_frame_no++;
    if (dump_frame_no == 100 || dump_frame_no == 300 || dump_frame_no == 600)
    {
      char name[32];
      SDFile *f;
      snprintf(name, sizeof(name), "frame%d.565", dump_frame_no);
      f = pd->file->open(name, kFileWrite);
      if (f)
      {
        pd->file->write(f, screen_pixels, GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT * 2);
        pd->file->close(f);
        pd->system->logToConsole("gpsp: dumped %s", name);
      }
    }
  }
#endif

  return 1;
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg)
{
  (void)arg;

  switch (event)
  {
    case kEventInit:
      pd = playdate;

#ifdef TARGET_PLAYDATE
      pd_syscalls_pd = pd;
#endif
      pd_filestream_init(pd);
      pd_render_init(pd);
      pd_audio_init(pd);
      pd->system->setAutoLockDisabled(1);

      gba_screen_pixels = screen_pixels;
      libretro_supports_bitmasks = true;
      retro_set_input_state(pd_input_state);

#if defined(HAVE_DYNAREC) && defined(TARGET_PLAYDATE)
      /* Device only: the simulator build may host the TRANSLATOR (for
       * PD_TRANSLATE_DUMP) but must never execute emitted Thumb-2. */
      thumb2_cache_sync_cb = pd->system->clearICache;
      dynarec_enable = 1;
#endif
#ifdef PD_SCHED_STATS
      {
        extern float (*pd_elapsed_cb)(void);
        pd_elapsed_cb = pd->system->getElapsedTime;
      }
#endif

      init_gamepak_buffer();
      init_sound();

      /* Dev fallback: a test.gba bundled in Source/ boots straight in. */
      {
        SDFile *f = pd->file->open("test.gba", kFileRead);
        if (f)
        {
          pd->file->close(f);
          strcpy(selected_rom, "test.gba");
        }
        else
        {
          init_rom_picker();
        }
      }
      rebuild_menu(1);

      pd->system->logToConsole("gpsp: Playdate build " __DATE__ " " __TIME__);
#ifdef PD_LAZY_LINK
      pd->system->logToConsole("gpsp: lazy direct links active");
#endif
#ifdef HAVE_DYNAREC
      /* Written to a file so it survives crashes: where did the loader put
       * the caches this boot? (OS 3.1.0 vs 3.0.6 placement question.) */
      {
        extern u8 rom_translation_cache[];
        extern u8 ram_translation_cache[];
        char amsg[120];
        SDFile *af = pd->file->open("addr.txt", kFileWrite);
        int alen = snprintf(amsg, sizeof(amsg),
                            "romtx=%p ramtx=%p reg=%p frame=%p\n",
                            (void *)rom_translation_cache,
                            (void *)ram_translation_cache,
                            (void *)reg, __builtin_frame_address(0));
        pd->system->logToConsole("gpsp: %s", amsg);
        if (af)
        {
          pd->file->write(af, amsg, alen);
          pd->file->close(af);
        }
      }
#endif
#ifdef PD_TCM_PROBE
      {
        extern void pd_tcm_probe_run(PlaydateAPI *pd);
        pd_tcm_probe_run(pd);
      }
#endif
#ifdef PD_JIT_SMOKE
      {
        extern void pd_jit_smoke_run(PlaydateAPI *pd);
        pd_jit_smoke_run(pd);
      }
#endif
      pd->system->setUpdateCallback(update, pd);
      break;

    case kEventTerminate:
    case kEventLock:
    case kEventLowPower:
      write_save_ram();
      break;

    default:
      break;
  }

  return 0;
}
