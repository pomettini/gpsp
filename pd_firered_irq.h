#ifndef PD_FIRERED_IRQ_H
#define PD_FIRERED_IRQ_H

#define PD_FIRERED_IRQ_RETURN_PC 0x00003F00U

#ifndef __ASSEMBLER__

#include "common.h"

int pd_firered_irq_try_enter(void);
void pd_firered_irq_return(void);
extern u32 pd_firered_irq_matched;

#endif

#endif
