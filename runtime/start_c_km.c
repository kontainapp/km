#include "km_hcalls.h"
#include "pthread_impl.h"
#include "stdio_impl.h"

extern int main(int argc, char** argv);

static void dummy_0()
{
}
weak_alias(dummy_0, __pthread_tsd_run_dtors);
weak_alias(dummy_0, __do_orphaned_stdio_locks);

volatile size_t __attribute__((__weak__)) __pthread_tsd_size = 0;
void *__pthread_tsd_main[1] __attribute__((__weak__)) = { 0 };

/*
 * Common entry point used for both main() and pthread entry.
 * The purpose is to process return from the actual code (main or pthread entry) and pass to exit().
 * Entry point in ELF header, then in km as km_guest.km_ehdr.e_entry, placed there by km_load_elf.
 */
_Noreturn void __start_c__(long is_main_argc, char** argv)
{
   int rc;

   if (is_main_argc == 0) {
      struct pthread* self = __pthread_self();
      rc = (int)self->start(self->start_arg);
   } else {
      (void)__pthread_tsd_size;
      rc = main(is_main_argc, argv);
   }
   __pthread_tsd_run_dtors();
   __do_orphaned_stdio_locks();
   while (1) {
      __syscall1(SYS_exit, rc);
   }
}

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
