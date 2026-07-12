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

void pd_filestream_init(PlaydateAPI *pd);
#ifdef TARGET_PLAYDATE
extern PlaydateAPI *pd_syscalls_pd;
#endif

/* Globals the core expects the frontend to define (libretro.c upstream). */
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

/* Audio: drained every frame so the core's ring indices stay in a sane
 * range. Phase 1 discards the samples; Playdate output is Phase 3. */
#define AUDIO_DRAIN_FRAMES 2048
static s16 audio_drain[AUDIO_DRAIN_FRAMES * 2];

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
static u32 perf_emu_ms, perf_blit_ms, perf_window_start_ms;
static u32 perf_emu_max_ms;

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

  rom_picker_free();
  pd->graphics->clear(kColorBlack);
  want_picker = 0;
  picker_active = 0;
  rebuild_menu(0);
  perf_updates = perf_rendered = perf_skipped = 0;
  perf_emu_ms = perf_blit_ms = perf_emu_max_ms = 0;
  perf_window_start_ms = pd->system->getCurrentTimeMilliseconds();
  pd->system->logToConsole("gpsp: running %s", selected_rom);

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
  SDFile *f = pd->file->open("perf.log", kFileAppend);
  if (f)
  {
    char line[200];
    u32 wall = now - perf_window_start_ms;
    int len = snprintf(line, sizeof(line),
        "build %s %s | fs=%s | upd=%u rend=%u skip=%u | wall=%ums "
        "| emu avg=%u.%02ums max=%ums | blit avg=%u.%02ums\n",
        __DATE__, __TIME__, fs_options[frameskip_mode],
        (unsigned)perf_updates, (unsigned)perf_rendered,
        (unsigned)perf_skipped, (unsigned)wall,
        (unsigned)(perf_emu_ms / perf_updates),
        (unsigned)((perf_emu_ms * 100 / perf_updates) % 100),
        (unsigned)perf_emu_max_ms,
        (unsigned)(perf_rendered ? perf_blit_ms / perf_rendered : 0),
        (unsigned)(perf_rendered ? (perf_blit_ms * 100 / perf_rendered) % 100 : 0));
    pd->file->write(f, line, len);
    pd->file->close(f);
  }
  perf_updates = perf_rendered = perf_skipped = 0;
  perf_emu_ms = perf_blit_ms = perf_emu_max_ms = 0;
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
  u32 t0, t1, t2;
  int skip;

  (void)userdata;

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

  /* One emulated frame; interpreter path needs the sticky bits cleared
   * (page-eviction protection) exactly like the libretro loop. */
  t0 = pd->system->getCurrentTimeMilliseconds();
  skip_next_frame = skip;
  clear_gamepak_stickybits();
  execute_arm(execute_cycles);
  t1 = pd->system->getCurrentTimeMilliseconds();

  /* Keep the audio ring drained (samples discarded until Phase 3). */
  sound_read_samples(audio_drain, AUDIO_DRAIN_FRAMES);

  if (!skip)
    pd_render_frame(screen_pixels);
  t2 = pd->system->getCurrentTimeMilliseconds();

  pd->system->drawFPS(0, 0);

  last_update_ms = t2 - t0;
  pd_playbench_update();
  pd_playbench_report_frame((float)last_update_ms, skip ? 1 : 0);

  perf_updates++;
  perf_emu_ms += t1 - t0;
  if (t1 - t0 > perf_emu_max_ms)
    perf_emu_max_ms = t1 - t0;
  if (skip)
    perf_skipped++;
  else
  {
    perf_rendered++;
    perf_blit_ms += t2 - t1;
  }
  if (perf_updates >= PERF_WINDOW)
    perf_flush(t2);

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
      pd->system->setAutoLockDisabled(1);

      gba_screen_pixels = screen_pixels;
      libretro_supports_bitmasks = true;
      retro_set_input_state(pd_input_state);

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
