#ifndef PD_RENDER_H
#define PD_RENDER_H

#include <stdint.h>
#include "pd_api.h"

void pd_render_init(PlaydateAPI *pd);
/* Convert the core's 240x160 RGB565 output to 1-bit and push it, centered
 * on the 400x240 LCD. */
void pd_render_frame(const uint16_t *src);

#endif
