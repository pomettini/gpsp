#ifndef PD_MEMPROF_H
#define PD_MEMPROF_H

/* A prime interval avoids repeatedly sampling the same point in a short
 * guest loop. The first interval offsets power-on from steady-state samples. */
#define PD_MEMPROF_PERIOD     8191
#define PD_MEMPROF_INITIAL    4093
#define PD_MEMPROF_CAPACITY  32768
#define PD_MEMPROF_COUNT_REG    63

#ifndef __ASSEMBLER__

#include <stdint.h>

typedef struct
{
  uint32_t pc;
  uint32_t address;
  uint32_t kind;
} PDMemProfileRecord;

extern volatile uint32_t pd_memprof_count;
extern volatile uint32_t pd_memprof_dropped;
extern PDMemProfileRecord pd_memprof_records[PD_MEMPROF_CAPACITY];

#endif

#endif
