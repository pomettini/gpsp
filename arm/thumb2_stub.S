/* gameplaySP - Thumb-2 dynarec stub glue for Cortex-M7 (Playdate port).
 *
 * WORK IN PROGRESS: ported from arm/arm_stub.S per the Phase 4 workplan in
 * NOTES.md. Unified Thumb-2 syntax throughout; every code symbol needs
 * .thumb_func; the data section (guest arrays + translation caches) keeps
 * arm_stub.S's static .bss layout.
 */

#error "Thumb-2 stub port in progress - build without DYNAREC=1 (see NOTES.md Phase 4 workplan)"

.syntax unified
.thumb

.text
.align 2
