/* Phase-2 renderer: RGB565 -> 8-bit luminance via a 64KB LUT, 4x4 ordered
 * Bayer dither, packed straight into LCD framebuffer bytes (the 80px X
 * offset is exactly 10 bytes, so GBA pixels 8-align into columns 10..39).
 * Only rows that changed since the previous frame are pushed to the LCD.
 * See NOTES.md for the measured cost. */

#include <string.h>

#include "render.h"

#define GBA_W 240
#define GBA_H 160
#define GBA_ROW_BYTES (GBA_W / 8)             /* 30 */
#define X_BYTE ((LCD_COLUMNS - GBA_W) / 2 / 8) /* 10 */
#define Y_OFF ((LCD_ROWS - GBA_H) / 2)         /* 40 */

static PlaydateAPI *pd;

/* RGB565 -> 0..255 luminance, one byte per possible pixel value. 64KB;
 * the working set per frame is only the colors actually on screen. */
static uint8_t lum_lut[32768];   /* 555-indexed: g LSB dropped (1/64 of
                                  * luminance) to halve D-cache footprint */
#define LUM_IDX(c) ((((c) >> 1) & 0x7FE0) | ((c) & 0x1F))

/* Previous frame's packed rows, for changed-row detection. */
static uint8_t prev_rows[GBA_H][GBA_ROW_BYTES];

/* 4x4 Bayer matrix scaled to the 0..255 luminance range. */
static const uint8_t bayer4[4][4] = {
  {  15, 135,  45, 165 },
  { 195,  75, 225, 105 },
  {  60, 180,  30, 150 },
  { 240, 120, 210,  90 },
};

void pd_render_init(PlaydateAPI *playdate)
{
  unsigned c;

  pd = playdate;
  pd->display->setRefreshRate(50.0f);

  for (c = 0; c < 32768; c++)
  {
    unsigned r = (c >> 10) & 0x1F;
    unsigned g = (c >> 5) & 0x1F;
    unsigned b = c & 0x1F;
    /* lum = 0.30r + 0.59g + 0.11b on 5-bit channels, 0..255 out. */
    unsigned lum = (r * 79 + g * 154 + b * 29) >> 5;
    if (lum > 255)
      lum = 255;
    /* Contrast S-curve: the linear LUT dithers text and mid-dark
     * backgrounds into competing checkers (unreadable dialogue). Squaring
     * toward the extremes separates them onto opposite dither halves. */
    /* Dark half: square curve pushes text into solid black (verified
     * readable). NEUTRAL light grays drop to a dark dither level:
     * FireRed draws save-screen text at lum ~210 near-gray on white
     * ~253, and no Bayer pattern can separate those. The remap must
     * skip colorful brights (pale overworld tiles live at lum 197-224
     * but green-tinted), so it keys on channel spread staying small. */
    if (lum < 128)
      lum = (lum * lum) >> 7;
    else if (lum > 193 && lum < 241)
    {
      unsigned mx = r > g ? r : g;
      unsigned mn = r < g ? r : g;
      if (b > mx) mx = b;
      if (b < mn) mn = b;
      if (mx - mn <= 2)
        lum = 96;
    }
    lum_lut[c] = (uint8_t)lum;
  }

  memset(prev_rows, 0xAA, sizeof(prev_rows)); /* force first-frame push */
}

void pd_render_frame(const uint16_t *src)
{
  uint8_t *frame = pd->graphics->getFrame();
  int y, first_dirty = -1, last_dirty = -1;

  for (y = 0; y < GBA_H; y++)
  {
    const uint16_t *row = src + y * GBA_W;
    const uint8_t *bay = bayer4[y & 3];
    uint8_t packed[GBA_ROW_BYTES];
    int x8;

    for (x8 = 0; x8 < GBA_ROW_BYTES; x8++)
    {
      /* white = bit set; 8px -> 1 byte. u32 pair loads; equal pixels in
       * a pair (flat tiles) reuse one LUT lookup - the 555 LUT's random
       * accesses are the blit's D-cache bottleneck. */
      const uint32_t *p32 = (const uint32_t *)(row + x8 * 8);
      uint8_t b = 0;
      uint32_t w;
      uint8_t la, lb;

      w = p32[0];
      la = lum_lut[LUM_IDX(w & 0xFFFF)];
      lb = ((w >> 16) == (w & 0xFFFF)) ? la : lum_lut[LUM_IDX(w >> 16)];
      b |= (la > bay[0]) ? 0x80 : 0;
      b |= (lb > bay[1]) ? 0x40 : 0;
      w = p32[1];
      la = lum_lut[LUM_IDX(w & 0xFFFF)];
      lb = ((w >> 16) == (w & 0xFFFF)) ? la : lum_lut[LUM_IDX(w >> 16)];
      b |= (la > bay[2]) ? 0x20 : 0;
      b |= (lb > bay[3]) ? 0x10 : 0;
      w = p32[2];
      la = lum_lut[LUM_IDX(w & 0xFFFF)];
      lb = ((w >> 16) == (w & 0xFFFF)) ? la : lum_lut[LUM_IDX(w >> 16)];
      b |= (la > bay[0]) ? 0x08 : 0;
      b |= (lb > bay[1]) ? 0x04 : 0;
      w = p32[3];
      la = lum_lut[LUM_IDX(w & 0xFFFF)];
      lb = ((w >> 16) == (w & 0xFFFF)) ? la : lum_lut[LUM_IDX(w >> 16)];
      b |= (la > bay[2]) ? 0x02 : 0;
      b |= (lb > bay[3]) ? 0x01 : 0;
      packed[x8] = b;
    }

    if (memcmp(prev_rows[y], packed, GBA_ROW_BYTES) != 0)
    {
      memcpy(prev_rows[y], packed, GBA_ROW_BYTES);
      memcpy(frame + (y + Y_OFF) * LCD_ROWSIZE + X_BYTE, packed, GBA_ROW_BYTES);
      if (first_dirty < 0)
        first_dirty = y;
      last_dirty = y;
    }
  }

  if (first_dirty >= 0)
    pd->graphics->markUpdatedRows(first_dirty + Y_OFF, last_dirty + Y_OFF);
}
