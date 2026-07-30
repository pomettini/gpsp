#include "pd_funcprof.h"

#ifdef PD_FUNC_PROFILE

#include <string.h>

typedef struct
{
  double seconds;
  float start;
  uint32_t calls;
  uint32_t depth;
  uint32_t unmatched;
} PDFuncProfileSlot;

#define PD_CALLBACK_RECORDS 32
#define PD_CALLBACK_DEPTH 8

typedef struct
{
  uint32_t pc;
  uint32_t calls;
  double seconds;
} PDCallbackRecord;

typedef struct
{
  uint32_t record;
  float start;
} PDCallbackActive;

static PlaydateAPI *fp_pd;
static PDFuncProfileSlot fp_slots[PD_FUNCPROF_COUNT];
static PDCallbackRecord fp_callbacks[PD_CALLBACK_RECORDS];
static PDCallbackActive fp_callback_stack[PD_CALLBACK_DEPTH];
static uint32_t fp_callback_records;
static uint32_t fp_callback_depth;
static uint32_t fp_callback_dropped;
static double fp_active_seconds;
static float fp_active_start;
static int fp_running;
static int fp_reported;

static const char *const fp_names[PD_FUNCPROF_COUNT] = {
  "",
  "AnimateSprites",
  "RunTextPrinters",
  "BuildOamBuffer",
  "AddSpritesToOamBuffer",
  "SpriteCallbacks",
  "AnimateSprite"
};

void pd_funcprof_init(PlaydateAPI *api)
{
  fp_pd = api;
}

void pd_funcprof_reset(void)
{
  memset(fp_slots, 0, sizeof(fp_slots));
  memset(fp_callbacks, 0, sizeof(fp_callbacks));
  memset(fp_callback_stack, 0, sizeof(fp_callback_stack));
  fp_callback_records = 0;
  fp_callback_depth = 0;
  fp_callback_dropped = 0;
  fp_active_seconds = 0.0;
  fp_active_start = 0.0f;
  fp_running = 0;
  fp_reported = 0;
  fp_pd->system->logToConsole(
    "gpsp funcprof: timing FireRed guest functions");
}

void pd_funcprof_resume(void)
{
  uint32_t i;
  float now;

  if (fp_running)
    return;
  now = fp_pd->system->getElapsedTime();
  fp_active_start = now;
  fp_running = 1;
  for (i = 1; i < PD_FUNCPROF_COUNT; i++)
    if (fp_slots[i].depth)
      fp_slots[i].start = now;
  for (i = 0; i < fp_callback_depth; i++)
    fp_callback_stack[i].start = now;
}

void pd_funcprof_pause(void)
{
  uint32_t i;
  float now;

  if (!fp_running)
    return;
  now = fp_pd->system->getElapsedTime();
  fp_active_seconds += (double)(now - fp_active_start);
  for (i = 1; i < PD_FUNCPROF_COUNT; i++)
    if (fp_slots[i].depth)
      fp_slots[i].seconds += (double)(now - fp_slots[i].start);
  for (i = 0; i < fp_callback_depth; i++)
    fp_callbacks[fp_callback_stack[i].record].seconds +=
      (double)(now - fp_callback_stack[i].start);
  fp_running = 0;
}

static void fp_stamp_at(uint32_t token, float now)
{
  uint32_t id = token & 0xFFU;
  PDFuncProfileSlot *slot;

  if (!fp_running || id == 0 || id >= PD_FUNCPROF_COUNT)
    return;
  slot = &fp_slots[id];
  if (token & PD_FUNCPROF_EXIT)
  {
    if (!slot->depth)
    {
      slot->unmatched++;
      return;
    }
    slot->depth--;
    if (!slot->depth)
      slot->seconds += (double)(now - slot->start);

    if (id == PD_FUNCPROF_SPRITE_CALLBACKS && fp_callback_depth)
    {
      PDCallbackActive *active =
        &fp_callback_stack[--fp_callback_depth];
      fp_callbacks[active->record].seconds +=
        (double)(now - active->start);
    }
  }
  else
  {
    slot->calls++;
    if (!slot->depth)
      slot->start = now;
    slot->depth++;
  }
}

void pd_funcprof_stamp(uint32_t token)
{
  fp_stamp_at(token, fp_pd->system->getElapsedTime());
}

void pd_funcprof_callback_enter(uint32_t pc)
{
  uint32_t i;
  float now;

  if (!fp_running)
    return;
  now = fp_pd->system->getElapsedTime();
  fp_stamp_at(PD_FUNCPROF_SPRITE_CALLBACKS, now);

  for (i = 0; i < fp_callback_records; i++)
    if (fp_callbacks[i].pc == pc)
      break;
  if (i == fp_callback_records)
  {
    if (i == PD_CALLBACK_RECORDS)
    {
      fp_callback_dropped++;
      return;
    }
    fp_callbacks[i].pc = pc;
    fp_callback_records++;
  }
  fp_callbacks[i].calls++;
  if (fp_callback_depth < PD_CALLBACK_DEPTH)
  {
    fp_callback_stack[fp_callback_depth].record = i;
    fp_callback_stack[fp_callback_depth].start = now;
    fp_callback_depth++;
  }
  else
  {
    fp_callback_dropped++;
  }
}

void pd_funcprof_report(void)
{
  uint32_t i, rank;
  uint8_t printed[PD_CALLBACK_RECORDS] = {0};

  if (fp_reported)
    return;
  fp_reported = 1;
  fp_pd->system->logToConsole("gpsp funcprof: active %.3f ms",
                              fp_active_seconds * 1000.0);
  for (i = 1; i < PD_FUNCPROF_COUNT; i++)
  {
    PDFuncProfileSlot *slot = &fp_slots[i];
    double total_ms = slot->seconds * 1000.0;
    double average_us = slot->calls ?
      slot->seconds * 1000000.0 / slot->calls : 0.0;
    double share = fp_active_seconds ?
      slot->seconds * 100.0 / fp_active_seconds : 0.0;
    fp_pd->system->logToConsole(
      "gpsp funcprof: %s calls=%u total=%.3fms avg=%.2fus share=%.2f%% open=%u bad=%u",
      fp_names[i], (unsigned)slot->calls, total_ms, average_us, share,
      (unsigned)slot->depth, (unsigned)slot->unmatched);
  }
  for (rank = 0; rank < fp_callback_records && rank < 12; rank++)
  {
    uint32_t best = PD_CALLBACK_RECORDS;
    for (i = 0; i < fp_callback_records; i++)
      if (!printed[i] &&
          (best == PD_CALLBACK_RECORDS ||
           fp_callbacks[i].seconds > fp_callbacks[best].seconds))
        best = i;
    if (best == PD_CALLBACK_RECORDS)
      break;
    printed[best] = 1;
    fp_pd->system->logToConsole(
      "gpsp funcprof cb: pc=0x%08x calls=%u total=%.3fms avg=%.2fus",
      (unsigned)fp_callbacks[best].pc,
      (unsigned)fp_callbacks[best].calls,
      fp_callbacks[best].seconds * 1000.0,
      fp_callbacks[best].calls ?
        fp_callbacks[best].seconds * 1000000.0 /
          fp_callbacks[best].calls : 0.0);
  }
  fp_pd->system->logToConsole(
    "gpsp funcprof cb: records=%u open=%u dropped=%u",
    (unsigned)fp_callback_records, (unsigned)fp_callback_depth,
    (unsigned)fp_callback_dropped);
}

#else

void pd_funcprof_init(PlaydateAPI *api) { (void)api; }
void pd_funcprof_reset(void) {}
void pd_funcprof_resume(void) {}
void pd_funcprof_pause(void) {}
void pd_funcprof_stamp(uint32_t token) { (void)token; }
void pd_funcprof_callback_enter(uint32_t pc) { (void)pc; }
void pd_funcprof_report(void) {}

#endif
