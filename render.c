/* Naive Phase-1 renderer: RGB565 -> luminance -> 4x4 ordered Bayer dither
 * -> packed 1-bit rows, 240x160 centered on the 400x240 LCD.
 * Correctness first; the LUT/packed fast path is Phase 2 (see NOTES.md). */

#include "render.h"

#define GBA_W 240
#define GBA_H 160
#define X_OFF ((LCD_COLUMNS - GBA_W) / 2) /* 80 */
#define Y_OFF ((LCD_ROWS - GBA_H) / 2)    /* 40 */

static PlaydateAPI *pd;

/* 4x4 Bayer matrix scaled to the 0..255 luminance range. */
static const uint8_t bayer4[4][4] = {
  {  15, 135,  45, 165 },
  { 195,  75, 225, 105 },
  {  60, 180,  30, 150 },
  { 240, 120, 210,  90 },
};

void pd_render_init(PlaydateAPI *playdate)
{
  pd = playdate;
  pd->graphics->clear(kColorBlack);
  pd->display->setRefreshRate(50.0f);
}

void pd_render_frame(const uint16_t *src)
{
  uint8_t *frame = pd->graphics->getFrame();
  int x, y;

  for (y = 0; y < GBA_H; y++)
  {
    const uint16_t *row = src + y * GBA_W;
    uint8_t *out = frame + (y + Y_OFF) * LCD_ROWSIZE;
    const uint8_t *bay = bayer4[y & 3];

    for (x = 0; x < GBA_W; x++)
    {
      uint16_t p = row[x];
      /* RGB565 -> 0..255 luminance: r,b 5 bits, g 6 bits.
       * lum = 0.30r + 0.59g + 0.11b, in integer form. */
      unsigned r = (p >> 11) & 0x1F;
      unsigned g = (p >> 5) & 0x3F;
      unsigned b = p & 0x1F;
      unsigned lum = (r * 79 + g * 77 + b * 29) >> 5;
      unsigned sx = x + X_OFF;

      if (lum > bay[x & 3])
        out[sx >> 3] |= (0x80 >> (sx & 7));   /* white */
      else
        out[sx >> 3] &= ~(0x80 >> (sx & 7));  /* black */
    }
  }

  pd->graphics->markUpdatedRows(Y_OFF, Y_OFF + GBA_H - 1);
}
