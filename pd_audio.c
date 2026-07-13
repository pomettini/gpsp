/* Playdate audio output for gpSP. The core mixes PSG+DirectSound at a fixed
 * 65536 Hz (GBA_SOUND_FREQUENCY) into its ring; each frame the MAIN thread
 * pulls those samples, linearly resamples them to 44100 Hz and pushes them
 * into a lock-free ring. The Playdate audio thread only drains the ring
 * (generating on the audio thread corrupts state - vecx finding).
 * Underruns emit silence: when the game runs under real speed there are
 * simply not enough samples; pitch stays correct, gaps appear instead. */

#include <stdint.h>
#include <string.h>

#include "pd_api.h"
#include "common.h"

void pd_audio_init(PlaydateAPI *pd);
void pd_audio_frame(void);

/* 8192 output frames (~186ms at 44100) of headroom. Power of two. */
#define RING_SIZE 8192
#define RING_MASK (RING_SIZE - 1)

static int16_t ring_l[RING_SIZE];
static int16_t ring_r[RING_SIZE];
static volatile uint32_t ring_write; /* owned by the main thread */
static volatile uint32_t ring_read;  /* owned by the audio thread */

/* 16.16 phase step: 65536/44100 source frames per output frame. */
#define RESAMPLE_STEP ((u32)(((u64)GBA_SOUND_FREQUENCY << 16) / 44100))

static u32 resample_phase;      /* 16.16 position into the staging buffer */
static s16 carry_l, carry_r;    /* last source frame of the previous batch */
static int carry_valid;

/* Core samples staged here each frame (stereo interleaved). 65536Hz over a
 * 20ms update = ~1311 frames; leave slack for catch-up frames. */
#define STAGE_FRAMES 4096
static s16 stage[STAGE_FRAMES * 2];

static PlaydateAPI *pd;

static int pd_audio_source(void *context, int16_t *left, int16_t *right,
                           int len)
{
  uint32_t rd = ring_read;
  uint32_t avail = ring_write - rd;
  int i, n = (int)(avail < (uint32_t)len ? avail : (uint32_t)len);

  (void)context;

  for (i = 0; i < n; i++)
  {
    left[i] = ring_l[rd & RING_MASK];
    right[i] = ring_r[rd & RING_MASK];
    rd++;
  }
  for (; i < len; i++)
  {
    left[i] = 0;
    right[i] = 0;
  }
  ring_read = rd;
  return 1;
}

void pd_audio_init(PlaydateAPI *playdate)
{
  pd = playdate;
  pd->sound->addSource(pd_audio_source, NULL, 1);
}

void pd_audio_frame(void)
{
  u32 frames, out_space, produced = 0;
  u32 wr = ring_write;

  /* Mix the guest audio for the elapsed frame, then drain the core ring. */
  render_gbc_sound();
  frames = sound_read_samples(stage, STAGE_FRAMES);
  if (frames == 0)
    return;

  out_space = RING_SIZE - (wr - ring_read);

  /* Linear resample with a carried previous frame so batches join
   * seamlessly. Phase 0 means "at the carry frame". */
  if (!carry_valid)
  {
    carry_l = stage[0];
    carry_r = stage[1];
    carry_valid = 1;
    resample_phase = 1 << 16;
  }

  while (out_space > 0)
  {
    u32 idx = resample_phase >> 16;   /* source frame index; 0 = carry */
    u32 frac = resample_phase & 0xFFFF;
    s32 l0, r0, l1, r1;

    if (idx >= frames)
      break;

    if (idx == 0)
    {
      l0 = carry_l;
      r0 = carry_r;
    }
    else
    {
      l0 = stage[(idx - 1) * 2];
      r0 = stage[(idx - 1) * 2 + 1];
    }
    l1 = stage[idx * 2];
    r1 = stage[idx * 2 + 1];

    ring_l[wr & RING_MASK] = (int16_t)(l0 + (((l1 - l0) * (s32)frac) >> 16));
    ring_r[wr & RING_MASK] = (int16_t)(r0 + (((r1 - r0) * (s32)frac) >> 16));
    wr++;
    produced++;
    out_space--;
    resample_phase += RESAMPLE_STEP;
  }

  /* Rebase the phase for the next batch relative to its new carry. */
  carry_l = stage[(frames - 1) * 2];
  carry_r = stage[(frames - 1) * 2 + 1];
  if ((resample_phase >> 16) >= frames)
    resample_phase -= frames << 16;
  else
    resample_phase = 1 << 16; /* ring overflow: drop the remainder */

  ring_write = wr;
}
