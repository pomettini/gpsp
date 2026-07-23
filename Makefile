# Playdate port of gpSP. Builds gpsp.pdx (device + simulator).
# The original libretro build lives in Makefile.libretro:
#   make -f Makefile.libretro platform=unix
HEAP_SIZE  = 8388208
STACK_SIZE = 61800

PRODUCT = gpsp.pdx

# Locate the SDK.
SDK = ${PLAYDATE_SDK_PATH}
ifeq ($(SDK),)
	SDK = $(shell egrep '^\s*SDKRoot' ~/.Playdate/config | head -n 1 | cut -c9-)
endif

ifeq ($(SDK),)
$(error SDK path not found; set ENV value PLAYDATE_SDK_PATH)
endif

VPATH += .

# Playdate glue (C) + gpSP core (C). cpu.cc/video.cc are C++ TUs upstream
# (templated renderer/interpreter) and get their own g++ rules below.
# rom_picker_unit.c #includes the pd-rom-picker submodule source via UINCDIR.
SRC = playdate_main.c \
      render.c \
      pd_audio.c \
      pd_filestream.c \
      pd_syscalls.c \
      rom_picker_unit.c \
      main.c \
      gba_memory.c \
      savestate.c \
      input.c \
      sound.c \
      cheats.c \
      serial.c \
      gbp.c \
      rfu.c \
      serial_proto.c \
      pd_playbench_unit.c \
      pd_jit_smoke.c \
      pd_tcm_probe.c \
      pd_tcm_pool.c \
      pd_bios_hle.c \
      pd_m4a_hle.c \
      pd_firered_hle.c \
      pd_firered_irq.c \
      pd_iwram_stack.c \
      pd_dynarec_hoststub.c

# Non-.c translation units (common.mk's object list only handles .c).
SRC_XX = cpu.cc video.cc bios_data.S

UINCDIR = libretro libretro/libretro-common/include pd-rom-picker/src \
          pd-playbench/src
UASRC =
# Interpreter-only build (no HAVE_DYNAREC). 8MB ROM page cache, see NOTES.md.
# PD_SHELL_AUDIO: the shell owns the per-frame render_gbc_sound() call
# (currently skipped entirely — samples are discarded until Phase 3 audio).
UDEFS = -DROM_BUFFER_SIZE=8 -DPD_SHELL_AUDIO
UDEFS += $(UDEFS_EXTRA)
UADEFS =
ULIBDIR =
ULIBS =

# Accepted shipping performance profile. Set individual options to 0 for
# A/B builds; BENCH, SCHEDSTATS and the other diagnostic flags stay opt-in.
DYNAREC ?= 1
TCMPOOL ?= $(DYNAREC)
SOUND32K ?= 1
BIOSHLE ?= 1
NARROW ?= $(DYNAREC)
SCHEDBATCH ?= 1
SCHEDBATCH2 ?= $(SCHEDBATCH)
COMPACTMEM ?= $(DYNAREC)
M4AFAST ?= 0
SPRITEFAST ?= 0
IRQFAST ?= 0
STACKFAST ?= 0
LAZYLINK ?= 0

# make BENCH=1: scripted-input benchmark build (pd-playbench). The script
# comes from /Shared/Emulation/gba/bench_script.txt if present, else the
# bundled Source/bench_firered_intro.txt. Run `make clean` when toggling.
ifeq ($(BENCH),1)
UDEFS += -DPD_PLAYBENCH_ENABLED
endif

# make JITSMOKE=1: run the Thumb-2 emit-and-execute smoke test at boot
# (Phase 4 step 0, see pd_jit_smoke.c). Run `make clean` when toggling.
ifeq ($(JITSMOKE),1)
UDEFS += -DPD_JIT_SMOKE
endif

# make TCMPROBE=1: progressive fast-memory probe at boot (pd_tcm_probe.c).
# Crashes are expected while probing; relaunch until "probe done". Delete
# Data/tcm_stage.txt to re-run from stage 0. Run `make clean` when toggling.
ifeq ($(TCMPROBE),1)
UDEFS += -DPD_TCM_PROBE
endif

# Default: copy the dynarec memory handlers into an isolated heap pool
# (pd_tcm_pool.c). TCMPOOL=0 disables it; needs DYNAREC.
ifeq ($(TCMPOOL),1)
UDEFS += -DPD_TCM_POOL
endif
# TCMPOOL=2: bisect variant - copy+selftest the pool but do NOT patch the
# dispatch tables (handlers keep running from PSRAM).
ifeq ($(TCMPOOL),2)
UDEFS += -DPD_TCM_POOL -DPD_TCM_POOL_NOPATCH
endif

# make PPUHALF=1: half-resolution PPU (render even lines, duplicate odd).
ifeq ($(PPUHALF),1)
UDEFS += -DPD_PPU_HALF
endif

# Default: mix guest audio at 32.768kHz instead of 65.536kHz to reduce PSG
# mixing cost. SOUND32K=0 restores the cleaner 65.536kHz path.
ifeq ($(SOUND32K),1)
UDEFS += -DGBA_SOUND_FREQUENCY='(32*1024)'
endif

# Default: native CpuSet/CpuFastSet/LZ77 instead of emulated BIOS loops.
# BIOSHLE=0 restores the emulated BIOS implementations.
ifeq ($(BIOSHLE),1)
UDEFS += -DPD_BIOS_HLE
endif

# Default with the dynarec: 16-bit T1 encodings where exactly equivalent.
ifeq ($(NARROW),1)
UDEFS += -DPD_NARROW
endif

# Default: coalesce scheduler events (one per scanline, batched sound-timer
# fires). Timing skew <=272 cycles; SCHEDBATCH=0 restores precise events.
ifeq ($(SCHEDBATCH),1)
UDEFS += -DPD_SCHED_BATCH
endif

# Default with SCHEDBATCH: additionally fast-forward vdraw on skipped frames
# when nothing per-line is armed. SCHEDBATCH2=0 disables fast-forwarding.
ifeq ($(SCHEDBATCH2),1)
UDEFS += -DPD_SCHED_BATCH2
endif

# make LOOKUPCACHE=1: direct-mapped front cache for runtime block lookups
# (A/B experiment; cleared on translation-cache flushes).
ifeq ($(LOOKUPCACHE),1)
UDEFS += -DPD_LOOKUP_CACHE
endif

# make LAZYLINK=1: translate external direct-branch targets on first use
# instead of recursively compiling the reachable call graph. Each cold gate
# patches itself to the translated target, so steady-state branches stay direct.
ifeq ($(LAZYLINK),1)
ifneq ($(DYNAREC),1)
$(error LAZYLINK=1 requires DYNAREC=1)
endif
UDEFS += -DPD_LAZY_LINK
UADEFS += -DPD_LAZY_LINK
endif

# make FRAMEDUMP=1: write the raw RGB565 guest frame to Data/frame.bin
# every ~10s (visual-bug diagnosis; use without BENCH).
ifeq ($(FRAMEDUMP),1)
UDEFS += -DPD_FRAME_DUMP
endif

# make LITPOOL=1: per-block PC-literal pools in emitted code (4-byte LDR
# literal instead of 8-byte MOVW/MOVT; shrinks the executed stream).
ifeq ($(LITPOOL),1)
UDEFS += -DPD_LIT_POOL
endif

# make DUMPJIT=1: on device, dump the translation caches + branch hash
# near the end of a bench run (Data/jitrom.bin etc.) for offline analysis.
ifeq ($(DUMPJIT),1)
UDEFS += -DPD_JIT_DUMP_DEVICE
endif

# make M4ADUMP=1: write the guest's 32KB IWRAM to Data/iwram.bin after
# 300 updates. Used to capture runtime-copied m4a mixer code; no hot-path
# instrumentation, so the emulator runs at normal speed before the write.
ifeq ($(M4ADUMP),1)
UDEFS += -DPD_M4A_DUMP
endif

# make M4AFAST=1: native, signature-guarded FireRed SoundMainRAM interpolator
# and final mixdown loops. Experimental until device A/B and audio agree.
ifeq ($(M4AFAST),1)
ifneq ($(DYNAREC),1)
$(error M4AFAST=1 requires DYNAREC=1)
endif
UDEFS += -DPD_M4A_HLE
UADEFS += -DPD_M4A_HLE
endif

# make SPRITEFAST=1: native, ROM-signature-guarded FireRed US 1.0 sprite
# coordinate/priority/sort/matrix passes. Experimental device A/B path.
ifeq ($(SPRITEFAST),1)
ifneq ($(DYNAREC),1)
$(error SPRITEFAST=1 requires DYNAREC=1)
endif
UDEFS += -DPD_FIRERED_SPRITE_HLE
UADEFS += -DPD_FIRERED_SPRITE_HLE
endif

# make IRQFAST=1: signature-guarded FireRed HBlank interrupt bridge.  The
# dynamic scanline callback still runs as guest code; only fixed wrappers are
# native.  Intended for battle scenes, which enable HBlank IRQs every line.
ifeq ($(IRQFAST),1)
ifneq ($(DYNAREC),1)
$(error IRQFAST=1 requires DYNAREC=1)
endif
UDEFS += -DPD_FIRERED_IRQ_HLE
UADEFS += -DPD_FIRERED_IRQ_HLE
endif

# make STACKFAST=1: collapse each Thumb PUSH to one native IWRAM copy after
# a runtime region check. Non-IWRAM stacks retain the normal memory handlers.
ifeq ($(STACKFAST),1)
ifneq ($(DYNAREC),1)
$(error STACKFAST=1 requires DYNAREC=1)
endif
UDEFS += -DPD_IWRAM_STACK_FAST
UADEFS += -DPD_IWRAM_STACK_FAST
endif

# Default with the dynarec: memory ops call shared stub dispatchers instead
# of inlining region dispatch at every site. COMPACTMEM=0 restores the
# original per-site dispatch sequences.
ifeq ($(COMPACTMEM),1)
UDEFS += -DPD_COMPACT_MEM
endif

# make MEMPROFILE=1: sample one in every 8191 dynarec memory operations and
# write Data/memprof.bin when the playbench script finishes. Diagnostic only:
# the dispatcher countdown intentionally perturbs benchmark timing.
ifeq ($(MEMPROFILE),1)
ifneq ($(DYNAREC),1)
$(error MEMPROFILE=1 requires DYNAREC=1)
endif
ifneq ($(COMPACTMEM),1)
$(error MEMPROFILE=1 requires COMPACTMEM=1)
endif
ifneq ($(BENCH),1)
$(error MEMPROFILE=1 requires BENCH=1)
endif
UDEFS += -DPD_MEM_PROFILE
UADEFS += -DPD_MEM_PROFILE
endif

# make SCHEDSTATS=1: count+sample update_gba scheduler round-trips
# (perf.log gains a "gba" field). Tiny per-call overhead; A/B only.
ifeq ($(SCHEDSTATS),1)
UDEFS += -DPD_SCHED_STATS
endif

# make PPUOFF=1: skip ALL rendering to measure the PPU's share of frame
# time (screen freezes by design; read perf.log's emu avg).
ifeq ($(PPUOFF),1)
UDEFS += -DPD_PPU_OFF
endif

# make PPUSTATS=1: count rendered scanlines by video mode, window setup,
# color effect and active-layer count. Diagnostic only; appended to perf.log.
ifeq ($(PPUSTATS),1)
UDEFS += -DPD_PPU_STATS
endif

# make INLINEMEM=1: inline load fastpaths in emitted code (A/B experiment).
ifeq ($(INLINEMEM),1)
UDEFS += -DPD_INLINE_MEM
endif

# Thumb-2 dynarec (arm/thumb2_emit.h), ON by default since bring-up passed
# (FireRed 2.5x, stable in gameplay). DYNAREC=0 builds the interpreter for
# A/B comparisons. The dynarec executes on DEVICE only; the simulator build
# hosts just the translator (PD_TRANSLATE_DUMP).
# SMALL_TRANSLATION_CACHE: 2MB ROM + 384KB RAM caches (16MB budget).
ifeq ($(DYNAREC),1)
UDEFS += -DHAVE_DYNAREC -DTHUMB2_ARCH -DSMALL_TRANSLATION_CACHE
# The stub reserves the translation caches in .bss: the assembler must see
# the same cache-size defines as the C side (ADEFS != DEFS in common.mk).
UADEFS += -DHAVE_DYNAREC -DTHUMB2_ARCH -DSMALL_TRANSLATION_CACHE
SRC += cpu_threaded.c
endif

include $(SDK)/C_API/buildsupport/common.mk

OPT = -O2 -falign-functions=16 -fomit-frame-pointer

# The interpreter (cpu.cc) is one giant macro-expanded function; at -O0 its
# stack frame alone overflows the simulator thread's stack. Always optimize
# the simulator build.
SIMCOMPILER = clang -g -O2

# Device link: common.mk expanded the pdex.elf prerequisites from SRC at
# include time, so the extra objects ride in via ULIBS (link line) plus an
# explicit prerequisite line (build order).
EXTRA_OBJS = $(OBJDIR)/cpu.o $(OBJDIR)/video.o $(OBJDIR)/bios_data.o
ifeq ($(DYNAREC),1)
EXTRA_OBJS += $(OBJDIR)/thumb2_stub.o
endif
ULIBS += $(EXTRA_OBJS)
$(OBJDIR)/pdex.elf: $(EXTRA_OBJS)

$(OBJDIR)/thumb2_stub.o: arm/thumb2_stub.S | MKOBJDIR MKDEPDIR
	$(AS) -c $(MCFLAGS) $(ADEFS) -Wa,-amhls=$(OBJDIR)/thumb2_stub.lst $< -o $@

# Simulator: redefine the dylib recipe so the .cc/.S TUs are compiled in
# (clang picks the language per extension). The duplicate-recipe warning
# from make is expected.
$(OBJDIR)/pdex.${DYLIB_EXT}: $(SRC) $(SRC_XX) | MKOBJDIR
	$(SIMCOMPILER) $(DYLIB_FLAGS) -lm -DTARGET_SIMULATOR=1 -DTARGET_EXTENSION=1 $(UDEFS) $(INCDIR) -o $@ $(SRC) $(SRC_XX)

CXX = $(GCC)$(TRGT)g++ -g3
# CPFLAGS minus C-only warnings, plus freestanding C++.
CXXFLAGS = $(MCFLAGS) $(OPT) -gdwarf-2 -Wall -Wno-unused -Wno-unknown-pragmas \
           -fverbose-asm -Wdouble-promotion -mword-relocations -fno-common \
           -ffunction-sections -fdata-sections $(DEFS) \
           -fno-exceptions -fno-rtti -fno-threadsafe-statics

# -O3 on the interpreter: measured on device before/after (see NOTES.md).
$(OBJDIR)/cpu.o: cpu.cc | MKOBJDIR MKDEPDIR
	$(CXX) -c $(filter-out -O2,$(CXXFLAGS)) -O3 -MD -MP -MF $(DEPDIR)/$(@F).d -I . $(INCDIR) $< -o $@

# -O3 on the translator: shrinks cold-cache translation bursts.
$(OBJDIR)/cpu_threaded.o: cpu_threaded.c | MKOBJDIR MKDEPDIR
	$(CC) -c $(filter-out -O2,$(CPFLAGS)) -O3 -I . $(INCDIR) $< -o $@

$(OBJDIR)/video.o: video.cc | MKOBJDIR MKDEPDIR
	$(CXX) -c $(CXXFLAGS) -MD -MP -MF $(DEPDIR)/$(@F).d -I . $(INCDIR) $< -o $@

# NOTE: do not use $(ASFLAGS) here: its -Wa,-amhls=$(<:.s=.lst) does not
# substitute a capital .S, so the assembler would write its listing OVER
# the source file.
$(OBJDIR)/bios_data.o: bios_data.S | MKOBJDIR MKDEPDIR
	$(AS) -c $(MCFLAGS) $(ADEFS) -Wa,-amhls=$(OBJDIR)/bios_data.lst $< -o $@

PLAYDATE_GAMES ?= /Volumes/PLAYDATE/Games

.PHONY: install _push

install: device
	@test -d "$(PLAYDATE_GAMES)" || (echo "Playdate volume not mounted at $(PLAYDATE_GAMES)" && exit 1)
	$(RM) -rf "$(PLAYDATE_GAMES)/$(PRODUCT)"
	COPYFILE_DISABLE=1 cp -R "$(PRODUCT)" "$(PLAYDATE_GAMES)/"
	-dot_clean -m "$(PLAYDATE_GAMES)/$(PRODUCT)"

_push: install
