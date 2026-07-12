/* Build glue for the pd-playbench submodule, same pattern as
 * rom_picker_unit.c. Compiled into every build but empty unless
 * PD_PLAYBENCH_ENABLED is defined (make BENCH=1): when disabled, the header
 * provides static-inline no-ops, so the shell calls it unconditionally and
 * the library source must stay out of the TU. Remember `make clean` when
 * toggling BENCH — a define change alone does not trigger rebuilds. */
#ifdef PD_PLAYBENCH_ENABLED
#include "pd_playbench.c"
#endif
