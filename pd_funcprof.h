#ifndef PD_FUNCPROF_H
#define PD_FUNCPROF_H

#include <stdint.h>
#include "pd_api.h"

enum
{
  PD_FUNCPROF_ANIMATE_SPRITES = 1,
  PD_FUNCPROF_RUN_TEXT_PRINTERS,
  PD_FUNCPROF_BUILD_OAM_BUFFER,
  PD_FUNCPROF_ADD_SPRITES_OAM,
  PD_FUNCPROF_SPRITE_CALLBACKS,
  PD_FUNCPROF_ANIMATE_SPRITE,
  PD_FUNCPROF_SLICE_MAIN,
  PD_FUNCPROF_COUNT
};

#define PD_FUNCPROF_EXIT 0x100U

static inline uint32_t pd_funcprof_token_for_pc(uint32_t pc)
{
  switch (pc)
  {
    case 0x08006B5CU: return PD_FUNCPROF_ANIMATE_SPRITES;
    case 0x08006B9CU: return PD_FUNCPROF_EXIT |
                            PD_FUNCPROF_ANIMATE_SPRITES;
    case 0x08002DE8U: return PD_FUNCPROF_RUN_TEXT_PRINTERS;
    case 0x08002E5AU: return PD_FUNCPROF_EXIT |
                            PD_FUNCPROF_RUN_TEXT_PRINTERS;
    case 0x08006BA8U: return PD_FUNCPROF_BUILD_OAM_BUFFER;
    case 0x08006BE2U: return PD_FUNCPROF_EXIT |
                            PD_FUNCPROF_BUILD_OAM_BUFFER;
    case 0x08006F04U: return PD_FUNCPROF_ADD_SPRITES_OAM;
    case 0x08006F70U: return PD_FUNCPROF_EXIT |
                            PD_FUNCPROF_ADD_SPRITES_OAM;
#ifndef PD_FUNC_PROFILE_TRANSITION
    case 0x08006B82U: return PD_FUNCPROF_EXIT |
                            PD_FUNCPROF_SPRITE_CALLBACKS;
    case 0x08007824U: return PD_FUNCPROF_ANIMATE_SPRITE;
    case 0x0800785AU: return PD_FUNCPROF_EXIT |
                            PD_FUNCPROF_ANIMATE_SPRITE;
#endif
    case 0x080D3220U: return PD_FUNCPROF_SLICE_MAIN;
    case 0x080D32EAU: return PD_FUNCPROF_EXIT |
                            PD_FUNCPROF_SLICE_MAIN;
    default: return 0;
  }
}

void pd_funcprof_init(PlaydateAPI *api);
void pd_funcprof_reset(void);
void pd_funcprof_resume(void);
void pd_funcprof_pause(void);
void pd_funcprof_stamp(uint32_t token);
void pd_funcprof_callback_enter(uint32_t pc);
void pd_funcprof_report(void);
void pd_funcprof_transition_start(void);
void pd_funcprof_transition_stop(void);

#endif
