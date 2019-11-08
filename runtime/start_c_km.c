#include "km_hcalls.h"

/*
 * Common handler for all interrupts, exceptions and traps in the guest.
 * Since the monitor does all the heavy lifting, this function does not return directly.
 * If a return into the guest is required, the monitor will set everything up to return
 * the right place.
 */
_Noreturn void __km_handle_interrupt(void)
{
   km_hc_args_t args;
   while (1) {
      // Should not return
      km_hcall(HC_guest_interrupt, &args);
   }
}
