/* gameplaySP - Thumb-2 dynarec backend for Cortex-M7 (Playdate port).
 *
 * WORK IN PROGRESS: this backend is being ported from arm/arm_emit.h per
 * the Phase 4 workplan in NOTES.md. Register conventions carry over 1:1
 * (r3-r8 allocatable, r9 flags, r11 reg_base, r12 cycles); encodings come
 * from arm/thumb2_codegen.h; conditional execution uses IT blocks or
 * branch-around; the stub glue is arm/thumb2_stub.S.
 *
 * Until the port is complete, DYNAREC=1 builds fail here on purpose. */

#ifndef THUMB2_EMIT_H
#define THUMB2_EMIT_H

#include "thumb2_codegen.h"

#error "Thumb-2 backend port in progress - build without DYNAREC=1 (see NOTES.md Phase 4 workplan)"

#endif /* THUMB2_EMIT_H */
