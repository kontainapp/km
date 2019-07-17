#include "km_hcalls.h"
#include "pthread_impl.h"
#include "stdio_impl.h"
#include "unistd.h"

extern int main(int argc, char** argv);

static void dummy()
{
}
weak_alias(dummy, _init);
weak_alias(dummy, _fini);

extern weak hidden void (*const __init_array_start)(void), (*const __init_array_end)(void);
extern weak hidden void (*const __fini_array_start)(void), (*const __fini_array_end)(void);

hidden void __libc_start_init(void)
{
   _init();
   uintptr_t a = (uintptr_t)&__init_array_start;
   for (; a < (uintptr_t)&__init_array_end; a += sizeof(void (*)())) {
      (*(void (**)(void))a)();
   }
}

hidden void __libc_exit_fini(void)   // called from exit
{
   uintptr_t a = (uintptr_t)&__fini_array_end;
   for (; a > (uintptr_t)&__fini_array_start; a -= sizeof(void (*)())) {
      (*(void (**)())(a - sizeof(void (*)())))();
   }
   _fini();
}

/*
 * Entry point in ELF header, then in km as km_guest.km_ehdr.e_entry, placed there by km_load_elf.
 */
_Noreturn void __start_c__(long argc, char** argv)
{
   (void)__pthread_tsd_size;
   __environ = argv + argc + 1;
   __libc_start_init();
   exit(main(argc, argv));
}

/*
 * Entry point for pthreads. Used by km to start threads. Refered from pthread_create to make an
 * undefined function reference
 */
_Noreturn volatile void __start_thread__(void* (*start)(void*), void* arg)
{
   pthread_exit(start(arg));
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
