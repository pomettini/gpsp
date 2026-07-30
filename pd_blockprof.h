#ifndef PD_BLOCKPROF_H
#define PD_BLOCKPROF_H

/* Sample translated block entries rather than memory operations. The trace
 * identifies CPU-heavy guest code even when its memory traffic is diffuse. */
#define PD_BLOCKPROF_PERIOD       4093
#define PD_BLOCKPROF_INITIAL      2053
#define PD_BLOCKPROF_CAPACITY    32768
#define PD_BLOCKPROF_COUNT_REG      62

#ifndef __ASSEMBLER__

#include <stdint.h>

extern volatile uint32_t pd_blockprof_count;
extern volatile uint32_t pd_blockprof_dropped;
extern uint32_t pd_blockprof_records[PD_BLOCKPROF_CAPACITY];

#endif

#endif
